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
            static std::vector<int> interleave(const std::vector<int>& input, const int16_t n_cbps, const int16_t n_bpsc);
    };
}

#endif //WIFI80211A_DATA_INTERLEAVER_H
