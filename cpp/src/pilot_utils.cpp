//
// Created by Kanishka on 4/19/2026.
//

#include "phy/pilot_utils.h"

namespace wifi80211a {

    double PilotLFSR::next_polarity()
    {
        // Taps at positions 7 and 4 (1-indexed) → bits 6 and 3 in a 7-bit register.
        const int out_bit  = (state_ >> 6) & 1;
        const int feedback = out_bit ^ ((state_ >> 3) & 1);
        state_ = static_cast<uint8_t>(((state_ << 1) | feedback) & 0x7F);
        return feedback ? -1.0 : +1.0; // BPSK: 0 → +1, 1 → −1
    }

    void PilotLFSR::reset()
    {
        state_ = INITIAL_STATE;
    }

} // namespace wifi80211a
