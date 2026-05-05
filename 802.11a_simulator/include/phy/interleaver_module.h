//
// Created by Kanishka on 4/7/2026.
//

#ifndef WIFI80211A_DATA_INTERLEAVER_H
#define WIFI80211A_DATA_INTERLEAVER_H
#include <cstdint>
#include <vector>

namespace wifi80211a
{
    class InterleaverModule
    {
        public:
            InterleaverModule() = default;
            static std::pmr::vector<int> interleave(const std::pmr::vector<int>& input, int n_cbps, int n_bpsc);
            static std::pmr::vector<int> deinterleave(const std::pmr::vector<int>& input, int n_cbps, int n_bpsc);
        };
}

#endif //WIFI80211A_DATA_INTERLEAVER_H
