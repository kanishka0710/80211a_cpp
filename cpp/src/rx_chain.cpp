//
// C++ port of python/rx_chain.py
//

#include "phy/rx_chain.h"

#include <algorithm>
#include <cmath>

#include "phy/equalizer_module.h"
#include "phy/fec_module.h"
#include "phy/interleaver_module.h"
#include "phy/modulation_module.h"
#include "phy/ofdm_module.h"

namespace wifi80211a
{
    namespace
    {
        complexVector apply_cfo_correction(const complexVector& signal, const double cfo_hz, const double Fs)
        {
            complexVector corrected(signal.size());
            for (std::size_t n = 0; n < signal.size(); n++)
            {
                const double phase = -2.0 * M_PI * cfo_hz * static_cast<double>(n) / Fs;
                corrected[n] = signal[n] * std::exp(std::complex<double>(0.0, phase));
            }
            return corrected;
        }
    }

    RxResult receive(
        const complexVector& rx_signal,
        const LinkSettings& link_settings,
        const std::uint8_t scrambler_seed)
    {
        const double Fs      = link_settings.getNFFT() / link_settings.getT();
        const int n_data_sc  = link_settings.getNumSubcarriers() - link_settings.getNumberOfPilots(); // 48
        const int n_cbps     = link_settings.getNCPBS();
        const int n_bpsc     = n_cbps / n_data_sc;

        // 1. Synchronization: coarse timing -> fine timing -> channel estimation
        const SyncResult sync = detect_and_sync(rx_signal, link_settings);

        // 2. Apply combined CFO correction to the full signal
        const complexVector corrected = apply_cfo_correction(rx_signal, sync.cfo_hz, Fs);

        // 3. Strip preamble and extract the data portion
        const int data_start = sync.packet_start + kPreambleLength;
        complexVector data_signal;
        if (data_start >= 0 && data_start < static_cast<int>(corrected.size()))
        {
            data_signal.assign(corrected.begin() + data_start, corrected.end());
        }

        // 4. OFDM demodulate: CP removal + FFT + pilot extraction
        const OFDMDemodResult ofdm_result = demodulate(link_settings, data_signal);

        // 5. Per-subcarrier equalization using the LTF-derived channel estimate
        const complexVector equalized = equalize_with_ltf(ofdm_result.freq_bins, sync.H, link_settings);

        // 6. Hard-decision constellation de-mapping -> bits
        const std::vector<int> detected_bits = map_constellation_to_bits(
            equalized, link_settings.getModulationType(), n_bpsc);

        // 7. De-interleave per OFDM symbol block
        const int num_symbols = static_cast<int>(detected_bits.size()) / n_cbps;
        std::vector<int> deinterleaved_bits(static_cast<std::size_t>(num_symbols) * n_cbps);
        for (int i = 0; i < num_symbols; i++)
        {
            const std::vector<int> block(
                detected_bits.begin() + i * n_cbps, detected_bits.begin() + (i + 1) * n_cbps);
            const std::vector<int> deinterleaved_block = deinterleave(block, n_cbps, n_bpsc);
            std::copy(deinterleaved_block.begin(), deinterleaved_block.end(),
                      deinterleaved_bits.begin() + i * n_cbps);
        }

        // 8. FEC RX: depuncture -> Viterbi -> descramble
        std::vector<int> rx_bits = performFECRX(deinterleaved_bits, link_settings.getCodingRate(), scrambler_seed);

        return RxResult{std::move(rx_bits), sync, equalized};
    }
}
