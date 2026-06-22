//
// Created by Kanishka on 4/19/2026.
//

#include "../include/phy/equalizer_module.h"
#include "phy/pilot_utils.h"

namespace wifi80211a
{
    complexVector EqualizerModule::perform_equalization(
        OFDMDemodResult& result,
        const LinkSettings& link_settings)
    {
        const int nFFT = link_settings.getNFFT();
        const int num_ofdm_syms = static_cast<int>(result.freq_bins.size()) / nFFT;
        if (result.reference_pilots.empty()) {
            PilotLFSR lfsr;
            result.reference_pilots.reserve(result.pilots.size());
            double prbs = 1.0;
            for (int i = 0; i < static_cast<int>(result.pilots.size()); i++) {
                if (i % 4 == 0)
                    prbs = lfsr.next_polarity();
                result.reference_pilots.emplace_back(prbs * PilotLFSR::POLARITY[i % 4], 0.0);
            }
        }

        complexVector equalized_symbols;
        equalized_symbols.reserve(num_ofdm_syms * 48); // 48 data subcarriers per symbol

        for (int i = 0; i < num_ofdm_syms; i++) {

            // Channel estimate at the 4 pilot positions for this symbol.
            std::array<std::complex<double>, 4> H;
            for (int p = 0; p < 4; p++)
                H[p] = result.pilots[i * 4 + p] / result.reference_pilots[i * 4 + p];

            // Iterate subcarriers in the same order as the TX (−26 → +26),
            // skipping DC and the four pilot positions.
            for (int sc = -26; sc <= 26; sc++) {
                if (sc == 0) continue;
                if (sc == -21 || sc == -7 || sc == 7 || sc == 21) continue;

                const int k = (sc > 0) ? sc : nFFT + sc; // FFT bin index

                // Nearest-neighbour interpolation across the 4 pilot positions.
                std::complex<double> H_k;
                if (sc <= -14) {
                    H_k = H[0]; 
                } else if (sc <   0) {
                    H_k = H[1]; 
                } else if (sc <=  14) {
                    H_k = H[2]; 
                } else {
                    H_k = H[3]; 
                }

                equalized_symbols.emplace_back(result.freq_bins[i * nFFT + k] / H_k);
            }
        }
        return equalized_symbols;
    }
}
