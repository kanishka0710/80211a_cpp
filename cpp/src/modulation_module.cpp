//
// Created by Kanishka on 4/7/2026.
//

#include "phy/modulation_module.h"

#include <cmath>
#include <complex>
#include <vector>

#include "phy/helpers.h"

namespace wifi80211a
{
    complexVector map_bits_to_constellation(std::vector<int> bits, ModulationTypes modulation, int n_bpsc)
    {
        complexVector result;
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

    std::vector<int> map_constellation_to_bits(const complexVector& symbols,
        ModulationTypes modulation, int n_bpsc)
    {   
        std::vector<int> bits;
        const double k_mod = normalization_constant.at(modulation);
        for (const auto& symbol : symbols) {
            switch (modulation) {
            case ModulationTypes::BPSK: {
                bits.push_back(symbol.real() > 0 ? 1 : 0);
                break;
            }
            case ModulationTypes::QPSK: {
                double min_distance = std::numeric_limits<double>::max();
                int closest_key = 0;
                for (const auto& [key, value] : qpsk_map) {
                    double distance = std::abs(symbol / k_mod - value);
                    min_distance = std::min(min_distance, distance);
                    if (distance == min_distance) {
                        closest_key = key;
                    }
                }
                auto unpacked = unpack_msb_(closest_key, n_bpsc);
                bits.insert(bits.end(), unpacked.begin(), unpacked.end());
                break;
            }
            case ModulationTypes::QAM16: {
                double min_distance = std::numeric_limits<double>::max();
                int closest_key = 0;
                for (const auto& [key, value] : qam16_map) {
                    double distance = std::abs(symbol / k_mod - value);
                    min_distance = std::min(min_distance, distance);
                    if (distance == min_distance) {
                        closest_key = key;
                    }
                }
                auto unpacked = unpack_msb_(closest_key, n_bpsc);
                bits.insert(bits.end(), unpacked.begin(), unpacked.end());
                break;
            }
            case ModulationTypes::QAM64: {
                double min_distance = std::numeric_limits<double>::max();
                int closest_key = 0;
                for (const auto& [key, value] : qam64_map) {
                    double distance = std::abs(symbol / k_mod - value);
                    min_distance = std::min(min_distance, distance);
                    if (distance == min_distance) {
                        closest_key = key;
                    }
                }
                auto unpacked = unpack_msb_(closest_key, n_bpsc);
                bits.insert(bits.end(), unpacked.begin(), unpacked.end());
                break;
            }
            default:
                break;
            }
        }
        return bits;
    }

    std::vector<int> unpack_msb_(int key, int n_bpsc)
    {
        std::vector<int> bits(n_bpsc);
        for (int b = 0; b < n_bpsc; ++b)
            bits[b] = (key >> (n_bpsc - 1 - b)) & 1;
        return bits;
    }

    int pack_msb_(const std::vector<int>& bits, const ModulationTypes modulation)
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
