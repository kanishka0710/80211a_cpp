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
    std::vector<int> performFEC(std::vector<int> data_bits, CodingRates coding_rate = CodingRates::R12,
        std::uint8_t scrambler_seed_7bit = 0);
    std::vector<int> performFECRX(std::vector<int>& data_bits, CodingRates rate, 
        std::uint8_t scrambler_seed_7bit);
    std::vector<int> data_scrambler_prbs(std::size_t n, std::uint8_t seed_7bit);
    std::vector<int> scramble_(std::vector<int> bits, std::uint8_t seed_7bit) ;
    int LFSRStep_(std::vector<int>& regs);
    std::vector<int> append_convolutional_tail_(std::vector<int> scrambled_bits);
    std::vector<int> convolutional_encoder_mother_(const std::vector<int>& scrambled_bits);
    std::vector<int> puncture_(std::vector<int> mother_coded_bits, CodingRates rate);
    std::vector<int> depuncture_(std::vector<int> rx_bits, CodingRates rate);
    std::vector<int> viterbi_decode_(std::vector<int> rx_bits);
    std::tuple<int[64][2], int[64][2], int[64][2]> precompute_trellis_();
}

#endif // WIFI80211A_FORWARD_ERROR_CORRECTION_H
