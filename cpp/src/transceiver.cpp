//
// Created by Kanishka on 5/4/2026.
//

#include "../include/phy/transceiver.h"

namespace wifi80211a
{
    complexVector generate_transmit_symbols(const std::vector<int>& bits, LinkSettings& linkSettings, PilotLFSR& pilotLfsr)
    {
        const int n_data = linkSettings.getNumSubcarriers() - linkSettings.getNumberOfPilots(); // 48
        const int n_cbps = linkSettings.getNCPBS();   // coded bits per OFDM symbol (48 for BPSK)
        const int n_bpsc = n_cbps / n_data;            // bits per subcarrier (1 for BPSK)

        std::vector<int> coded_bits = performFEC(
            bits, linkSettings.getCodingRate(), /*scrambler_seed_7bit=*/0x5B);

        const int pad = (n_cbps - static_cast<int>(coded_bits.size()) % n_cbps) % n_cbps;
        coded_bits.insert(coded_bits.end(), pad, 0);

        std::vector<int> interleaved_bits;
        interleaved_bits.reserve(coded_bits.size());
        for (std::size_t i = 0; i < coded_bits.size(); i += n_cbps) {
            std::vector<int> block(coded_bits.begin() + i, coded_bits.begin() + i + n_cbps);
            auto blk = interleave(block, n_cbps, n_bpsc);
            interleaved_bits.insert(interleaved_bits.end(), blk.begin(), blk.end());
        }

        auto symbols = map_bits_to_constellation(
            interleaved_bits, linkSettings.getModulationType(), n_bpsc);

        pilotLfsr.reset();
        return modulate(linkSettings, symbols, pilotLfsr);
    }


    std::vector<int> receive_symbols(complexVector& signal, LinkSettings& linkSettings)
    {
        const int n_data = linkSettings.getNumSubcarriers() - linkSettings.getNumberOfPilots(); // 48
        const int n_cbps = linkSettings.getNCPBS();   // coded bits per OFDM symbol (48 for BPSK)
        const int n_bpsc = n_cbps / n_data;            // bits per subcarrier (1 for BPSK)

        auto ofdm_result = demodulate(linkSettings, signal);
        auto equalized = perform_equalization(ofdm_result, linkSettings);
        auto rx_bits = map_constellation_to_bits(equalized, linkSettings.getModulationType(), n_bpsc);
        auto deinterleaved_pmr = deinterleave(rx_bits, n_cbps, n_bpsc);
        std::vector<int> deinterleaved_bits(deinterleaved_pmr.begin(), deinterleaved_pmr.end());
        return performFECRX(deinterleaved_bits, linkSettings.getCodingRate(), 0x5B);
    }

} // wifi80211a