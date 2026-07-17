//
// C++ port of python/tx_chain.py
//

#ifndef WIFI80211A_TX_CHAIN_H
#define WIFI80211A_TX_CHAIN_H

#include <cstdint>
#include <vector>

#include "phy/helpers.h"
#include "phy/link_settings.h"

namespace wifi80211a
{
    /// Full 802.11a transmitter chain.
    ///
    /// bits           — raw input bits (0/1 values)
    /// link_settings  — modulation + coding rate + PHY parameters
    /// scrambler_seed — 7-bit non-zero seed for the data scrambler
    ///
    /// Returns the TX signal:
    ///     [preamble (320 samples)] + [OFDM data symbols]
    ///
    /// Pipeline:
    ///     FEC (scramble -> convolutional encode -> puncture)
    ///     -> interleave (per OFDM symbol)
    ///     -> constellation mapping
    ///     -> OFDM modulate (pilots + IFFT + cyclic prefix)
    ///     -> prepend preamble (STF + LTF)
    complexVector generate_tx_signal(
        const std::vector<int>& bits,
        const LinkSettings& link_settings,
        std::uint8_t scrambler_seed = 0x5D);
}

#endif //WIFI80211A_TX_CHAIN_H
