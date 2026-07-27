//
// Created by Kanishka on 4/7/2026.
//

#ifndef WIFI80211A_FORWARD_ERROR_CORRECTION_H
#define WIFI80211A_FORWARD_ERROR_CORRECTION_H

#include <cstdint>
#include <utility>
#include <vector>

#include "phy/link_settings.h"

namespace wifi80211a
{
    constexpr int kServiceBits = 16;   // 17.3.5.1: 7 scrambler-sync zeros + 9 reserved zeros
    constexpr int kDataTailBits = 6;   // 17.3.5.2

    std::vector<int> performFEC(std::vector<int> data_bits, CodingRates coding_rate = CodingRates::R12,
        std::uint8_t scrambler_seed_7bit = 0);
    std::vector<int> performFECRX(std::vector<int>& data_bits, CodingRates rate, 
        std::uint8_t scrambler_seed_7bit);

    /// N_DBPS = N_CBPS * R -- data bits carried per OFDM symbol (Table 78).
    int compute_ndbps(int n_cbps, CodingRates coding_rate);

    /// Returns (N_SYM, N_PAD) per 17.3.5.4, Eq. (11)-(13), for a
    /// SERVICE(16) + PSDU + TAIL(6) message of the given PSDU length.
    std::pair<int, int> compute_data_field_sizing(int psdu_len_bits, int n_dbps);

    /// Builds and encodes the PLCP DATA field per 17.3.5: SERVICE(16 zeros)
    /// + PSDU + TAIL(6 zeros) + PAD(zeros), all scrambled together as one
    /// block, with the 6 TAIL bits then overwritten with unscrambled zeros
    /// (17.3.5.2), followed by rate-1/2 convolutional encoding and
    /// puncturing to `coding_rate`.
    std::vector<int> performFECDataField(std::vector<int> psdu_bits, CodingRates coding_rate,
        std::uint8_t scrambler_seed_7bit, int n_dbps);

    /// Inverse of performFECDataField. Returns PSDU + TAIL + PAD bits (i.e.
    /// everything after the 16-bit SERVICE field); the caller trims to the
    /// PSDU length carried in the SIGNAL field.
    std::vector<int> performFECDataFieldRX(std::vector<int>& bits, CodingRates coding_rate,
        std::uint8_t scrambler_seed_7bit);
    std::vector<int> data_scrambler_prbs(std::size_t n, std::uint8_t seed_7bit);
    std::vector<int> scramble_(std::vector<int> bits, std::uint8_t seed_7bit) ;
    int LFSRStep_(std::vector<int>& regs);
    std::vector<int> append_convolutional_tail_(std::vector<int> scrambled_bits);
    std::vector<int> convolutional_encoder_mother_(const std::vector<int>& scrambled_bits);
    std::vector<int> puncture_(std::vector<int> mother_coded_bits, CodingRates rate);
    std::tuple<std::vector<int>, std::vector<int>> depuncture_(std::vector<int> rx_bits, CodingRates rate);
    std::vector<int> viterbi_decode_(std::vector<int> rx_bits, std::vector<int> mask_bits);
    std::tuple<std::vector<std::vector<int>>, std::vector<std::vector<int>>, std::vector<std::vector<int>>> precompute_trellis_();
}

#endif // WIFI80211A_FORWARD_ERROR_CORRECTION_H
