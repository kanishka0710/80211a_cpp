#pragma once
#include <vector>
#include <complex>
#include <cstdint>
#include <memory_resource>
#include "phy/link_settings.h"

namespace wifi80211a {

    class OFDMModulator {
    public:
        OFDMModulator() = default;
        std::pmr::vector<std::complex<double>> modulate(LinkSettings link_settings, std::pmr::vector<std::complex<double>> data);

    private:
        // 7-bit LFSR for pilot polarity, poly x^7 + x^4 + 1, init all-ones
        uint8_t pilot_lfsr = 0x7F;

        // Returns +1 or -1 and advances the LFSR by one step
        double next_pilot_polarity();
    };
}
