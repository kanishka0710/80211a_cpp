//
// Created by Kanishka on 4/19/2026.
//

#pragma once

#include <array>
#include <cstdint>

namespace wifi80211a {

    /// Reusable 7-bit LFSR that generates the per-symbol pilot polarity sequence
    /// defined in IEEE 802.11a (poly x^7 + x^4 + 1, initial state all-ones).
    class PilotLFSR {
    public:
        static constexpr std::array<int, 4> POLARITY = {+1, +1, +1, -1};

        PilotLFSR() = default;

        /// Advances the LFSR by one step and returns +1.0 or −1.0.
        /// Call once per OFDM symbol, then multiply by POLARITY[p] for each pilot.
        double next_polarity();

        /// Resets to the initial seed. Useful for unit tests or re-processing.
        void reset();

    private:
        static constexpr uint8_t INITIAL_STATE = 0x7F;
        uint8_t state_ = INITIAL_STATE;
    };

} // namespace wifi80211a
