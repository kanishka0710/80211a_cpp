//
// Created by Kanishka on 4/7/2026.
//

#include "phy/modulation_module.h"

#include <cmath>
#include <complex>
#include <vector>

namespace wifi80211a
{
    std::vector<std::complex<double>> ModulationModule::map_bits_to_constellation(std::vector<int> bits, ModulationTypes modulation, int16_t n_bpsc)
    {
        std::vector<std::complex<double>> result;
        result.reserve(bits.size() / n_bpsc);
        const double k_mod = normalization_constant.at(modulation);

        for (size_t i = 0; i + n_bpsc <= bits.size(); i += n_bpsc)
        {
            const std::vector<int> slice(bits.begin() + i, bits.begin() + i + n_bpsc);
            int key = pack_msb_(slice, modulation);

            switch (modulation)
            {
            case ModulationTypes::BPSK:
                result.push_back(bpsk_map.at(key));
                break;
            case ModulationTypes::QPSK:
                result.push_back(qpsk_map.at(key) * k_mod);
                break;
            case ModulationTypes::QAM16:
                result.push_back(qam16_map.at(key) * k_mod);
                break;
            case ModulationTypes::QAM64:
                result.push_back(qam64_map.at(key) * k_mod);
                break;
            }
        }
        return result;
    }

    int ModulationModule::pack_msb_(const std::vector<int>& bits, const ModulationTypes modulation)
    {
        switch (modulation)
        {
        case ModulationTypes::BPSK:
            return bits[0];
        case ModulationTypes::QPSK:
            return (bits[0] << 1) | bits[1];
        case ModulationTypes::QAM16:
            return (bits[0] << 3) | (bits[1] << 2) | (bits[2] << 1) | bits[3];
        case ModulationTypes::QAM64:
            return (bits[0] << 5) | (bits[1] << 4) | (bits[2] << 3)
                 | (bits[3] << 2) | (bits[4] << 1) |  bits[5];
        default:
            return 0;
        }
    }
}
