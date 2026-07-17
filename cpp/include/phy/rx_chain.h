//
// C++ port of python/rx_chain.py
//

#ifndef WIFI80211A_RX_CHAIN_H
#define WIFI80211A_RX_CHAIN_H

#include <cstdint>
#include <vector>

#include "phy/helpers.h"
#include "phy/link_settings.h"
#include "phy/sync_module.h"

namespace wifi80211a
{
    /// 160-sample STF + 160-sample LTF.
    constexpr int kPreambleLength = 320;

    struct RxResult
    {
        std::vector<int> bits;
        SyncResult sync;
        complexVector equalized;
    };

    /// Full 802.11a receiver chain.
    ///
    /// rx_signal      — raw received complex baseband samples
    /// link_settings  — modulation + coding rate + PHY parameters (must match TX)
    /// scrambler_seed — 7-bit seed (must match TX)
    ///
    /// Pipeline:
    ///     sync (timing + CFO + channel estimate)
    ///     -> CFO correction
    ///     -> strip preamble
    ///     -> OFDM demodulate (CP strip + FFT)
    ///     -> LTF-based per-subcarrier equalization
    ///     -> constellation de-map (hard decision)
    ///     -> de-interleave
    ///     -> FEC RX (depuncture -> Viterbi -> descramble)
    RxResult receive(
        const complexVector& rx_signal,
        const LinkSettings& link_settings,
        std::uint8_t scrambler_seed = 0x5D);
}

#endif //WIFI80211A_RX_CHAIN_H
