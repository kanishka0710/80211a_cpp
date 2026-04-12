#include "phy/fec_module.h"
#include "phy/interleaver_module.h"
#include "phy/link_settings.h"
#include "phy/modulation_module.h"
#include "phy/ofdm_module.h"

#include <matplot/matplot.h>

#include <algorithm>
#include <complex>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <string>
#include <vector>

#include "phy/front_end_module.h"

int main()
{
    const std::vector<int> input_bits = {
        1,1,1,0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,1,
    };

    wifi80211a::LinkSettings link_settings(
        wifi80211a::ModulationTypes::BPSK,
        wifi80211a::CodingRates::R12);

    const int16_t n_data = link_settings.getNumSubcarriers() - link_settings.getNumberOfPilots(); // 48
    const int16_t n_cbps = link_settings.getNCPBS();   // coded bits per OFDM symbol (48 for BPSK)
    const int16_t n_bpsc = n_cbps / n_data;            // bits per subcarrier (1 for BPSK)

    // ── 1. FEC: scramble → tail → rate-1/2 convolutional encode → puncture ───
    std::vector<int> coded_bits = wifi80211a::FECModule::performFEC(
        input_bits, wifi80211a::CodingRates::R12, /*scrambler_seed_7bit=*/0x5B);

    // ── 2. Pad to OFDM-symbol boundary then interleave one block at a time ───
    const int pad = (n_cbps - static_cast<int>(coded_bits.size()) % n_cbps) % n_cbps;
    coded_bits.insert(coded_bits.end(), pad, 0);

    std::vector<int> interleaved_bits;
    interleaved_bits.reserve(coded_bits.size());
    for (std::size_t i = 0; i < coded_bits.size(); i += n_cbps) {
        std::vector<int> block(coded_bits.begin() + i, coded_bits.begin() + i + n_cbps);
        auto blk = wifi80211a::InterleaverModule::interleave(block, n_cbps, n_bpsc);
        interleaved_bits.insert(interleaved_bits.end(), blk.begin(), blk.end());
    }

    // ── 3. Constellation map ──────────────────────────────────────────────────
    auto syms_vec = wifi80211a::ModulationModule::map_bits_to_constellation(
        interleaved_bits, link_settings.getModulationType(), n_bpsc);

    std::pmr::vector<std::complex<double>> symbols(syms_vec.begin(), syms_vec.end());

    // ── 4. OFDM modulate: IFFT + cyclic prefix insertion ─────────────────────
    wifi80211a::OFDMModulator modulator;
    auto time_samples = modulator.modulate(link_settings, symbols);

    wifi80211a::FrontEndModule front_end_module;
    time_samples = front_end_module.pulse_shape(time_samples, link_settings.getT());
    auto tx_waveform = front_end_module.iq_modulate(time_samples, link_settings.getT());

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Input bits       : " << input_bits.size()       << '\n';
    std::cout << "After FEC + pad  : " << coded_bits.size()       << '\n';
    std::cout << "After interleave : " << interleaved_bits.size() << '\n';
    std::cout << "Symbols          : " << symbols.size()          << '\n';
    const int sym_len = link_settings.getNFFT() + link_settings.getCPLenData(); // 80
    std::cout << "Time samples     : " << tx_waveform.size()
              << "  (" << tx_waveform.size() / sym_len / 4
              << " OFDM symbols * " << sym_len << " samples)\n";

    // ── Matplot++: baseband waveform (I/Q + envelope) ─────────────────────────
    const std::size_t n = tx_waveform.size();
    std::vector<double> sample_idx(n);
    for (std::size_t i = 0; i < n; ++i) {
        sample_idx[i] = static_cast<double>(i);
    }

    matplot::figure();
    matplot::sgtitle("802.11a TX — baseband waveform");

    matplot::plot(sample_idx, tx_waveform, "-")->line_width(1.2).color("green");

    matplot::grid(true);
    matplot::title("TX Waveform");
    matplot::ylabel( "|s(n)|");
    matplot::xlabel( "Sample index");

    std::cout << "Showing Matplot++ figure (requires Gnuplot on PATH). Close window to exit.\n";
    matplot::show();

    return 0;
}
