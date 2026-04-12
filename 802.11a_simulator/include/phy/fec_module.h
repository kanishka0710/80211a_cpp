//
// Created by Kanishka on 4/7/2026.
//

#ifndef WIFI80211A_FORWARD_ERROR_CORRECTION_H
#define WIFI80211A_FORWARD_ERROR_CORRECTION_H

#include <cstdint>
#include <vector>

#include "phy/link_settings.h"

namespace wifi80211a
{

    /// 802.11a DATA-field transmit coding (before OFDM bit-interleaving / mapping).
    ///
    /// Intended TX order (implement inside `performFEC` or split into helpers as you go):
    /// 1. **Scramble** — XOR the DATA-field bit sequence with the PHY PRBS; LFSR initial state
    ///    is formed from the SERVICE field (802.11 clause for OFDM PHY).
    /// 2. **Tail** — After the scrambled data bits, append the **convolutional termination** bits
    ///    (zeros into the K=7 encoder; count per IEEE spec) so the trellis ends in the zero state.
    /// 3. **Conv. encode (mother)** — Rate **1/2**, K=7; two output bits per message bit; generator
    ///    polynomials and output bit **pair ordering** per standard figures.
    /// 4. **Puncture** — For higher RATEs, drop mother-code bits on a **fixed periodic pattern**
    ///    determined by `coding_rate` (802.11 tables); base mode uses unpunctured 1/2.
    ///
    /// Next stage in the PHY (usually **not** this class): **block interleaving**, padding to
    /// `N_CBPS` boundaries, constellation mapping, pilots/IFFT.
    class FECModule
    {
    public:
        FECModule() = default;

        /// @param data_bits              Bits of the DATA field in transmit order (SERVICE, PSDU,
        ///                               etc. — whatever your assembler produces **before** FEC).
        /// @param coding_rate            Selects puncturing; mother code is always rate 1/2.
        /// @param scrambler_seed_7bit    Seven least-significant bits that preload the scrambler
        ///                               register from SERVICE (see standard; often non-zero).
        static std::vector<int> performFEC(
            std::vector<int> data_bits,
            CodingRates coding_rate = CodingRates::R12,
            std::uint8_t scrambler_seed_7bit = 0);

        /// Returns the first `n` raw PRBS bits from the scrambler LFSR starting
        /// at `seed_7bit`, without XORing against any input data.
        static std::vector<int> data_scrambler_prbs(std::size_t n, std::uint8_t seed_7bit);

    private:
        static std::vector<int> scramble_(std::vector<int> bits, std::uint8_t seed_7bit) ;
        static int LFSRStep_(std::vector<int>& regs);
        static std::vector<int> append_convolutional_tail_(std::vector<int> scrambled_bits);
        static std::vector<int> convolutional_encoder_mother_(const std::vector<int>& scrambled_bits);
        static std::vector<int> puncture_(std::vector<int> mother_coded_bits, CodingRates rate);
    };
}

#endif // WIFI80211A_FORWARD_ERROR_CORRECTION_H
