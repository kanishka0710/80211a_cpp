//
// Created by Kanishka on 4/7/2026.
//

#include "phy/interleaver_module.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace wifi80211a
{
    std::pmr::vector<int> InterleaverModule::interleave(
        const std::pmr::vector<int>& input,
        const int n_cbps,
        const int n_bpsc)
    {
        std::pmr::vector<int> interleaved_output(input.size());
        for (int k = 0; k < n_cbps; k++)
        {
            int i = (n_cbps / 16) * (k % 16) + k / 16;
            int s = std::max(n_bpsc / 2, 1);
            int j = s * (i / s) + (i + n_cbps - (16 * i / n_cbps)) % s;
            interleaved_output[j] = input[k];
        }
        return interleaved_output;
    }

    std::pmr::vector<int> InterleaverModule::deinterleave(
        const std::pmr::vector<int>& input,
        const int n_cbps,
        const int n_bpsc)
    {
        std::pmr::vector<int> deinterleaved_output(input.size());
        for (int k = 0; k < n_cbps; k++) {
            int i = (n_cbps / 16) * (k % 16) + k / 16;
            int s = std::max(n_bpsc / 2, 1);
            int j = s * (i / s) + (i + n_cbps - (16 * i / n_cbps)) % s;
            deinterleaved_output[k] = input[j];
        }
        return deinterleaved_output;
    }
}
