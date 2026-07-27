//
// C++ port of python/tx_chain.py
//

#include "phy/tx_chain.h"

#include <algorithm>

#include "phy/fec_module.h"
#include "phy/interleaver_module.h"
#include "phy/modulation_module.h"
#include "phy/ofdm_module.h"
#include "phy/pilot_utils.h"
#include "phy/preamble_module.h"
#include "phy/signal_module.h"

namespace wifi80211a
{
    complexVector generate_tx_signal(
        const std::vector<int>& bits,
        const LinkSettings& link_settings,
        const std::uint8_t scrambler_seed)
    {
        const int n_data_sc = link_settings.getNumSubcarriers() - link_settings.getNumberOfPilots(); // 48
        const int n_cbps    = link_settings.getNCPBS();
        const int n_bpsc    = n_cbps / n_data_sc;

        // 1. FEC: build the DATA field (SERVICE + PSDU + TAIL + PAD, 17.3.5),
        // scramble -> convolutional encode -> puncture. N_PAD is solved so
        // the coded output lands on an exact multiple of n_cbps, so no
        // separate padding step is needed afterward.
        const int n_dbps = compute_ndbps(n_cbps, link_settings.getCodingRate());
        std::vector<int> fec_bits = performFECDataField(
            bits, link_settings.getCodingRate(), scrambler_seed, n_dbps);

        const int num_ofdm_symbols = static_cast<int>(fec_bits.size()) / n_cbps;

        // 2. Interleave (per OFDM symbol)
        std::vector<int> interleaved_bits(fec_bits.size());
        for (int i = 0; i < num_ofdm_symbols; i++)
        {
            const std::vector<int> block(
                fec_bits.begin() + i * n_cbps, fec_bits.begin() + (i + 1) * n_cbps);
            const std::vector<int> interleaved_block = interleave(block, n_cbps, n_bpsc);
            std::copy(interleaved_block.begin(), interleaved_block.end(),
                      interleaved_bits.begin() + i * n_cbps);
        }

        // 3. Constellation mapping
        const complexVector symbols = map_bits_to_constellation(
            interleaved_bits, link_settings.getModulationType(), n_bpsc);

        // 4. OFDM modulate (pilots + IFFT + cyclic prefix)
        PilotLFSR pilot_lfsr;
        const complexVector ofdm_signal = modulate(link_settings, symbols, pilot_lfsr);

        // 5. Prepend preamble (STF + LTF = 320 samples)
        complexVector tx_signal = generate_preamble(link_settings);

        // 6. Insert SIGNAL field (RATE + LENGTH for the DATA field that follows)
        int psdu_length_octets = static_cast<int>(bits.size()) / 8;
        const complexVector signal_header = create_signal_header(link_settings, psdu_length_octets);
        tx_signal.insert(tx_signal.end(), signal_header.begin(), signal_header.end());

        tx_signal.insert(tx_signal.end(), ofdm_signal.begin(), ofdm_signal.end());

        return tx_signal;
    }
}
