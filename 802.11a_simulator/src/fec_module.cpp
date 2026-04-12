//
// Created by Kanishka on 4/7/2026.
//

#include "phy/fec_module.h"

#include <cstddef>

namespace wifi80211a
{

    std::vector<int> FECModule::performFEC(
        std::vector<int> data_bits,
        const CodingRates coding_rate,
        const std::uint8_t scrambler_seed_7bit)
    {

        // Step 1 — Scrambler
        std::vector<int> scrambled_bits = scramble_(data_bits, scrambler_seed_7bit);

        // Step 2 — Convolutional tail
        std::vector<int> tailed_bits = append_convolutional_tail_(scrambled_bits);

        // Step 3 — Conv. encoder (mother 1/2)
        std::vector<int> mother_bits = convolutional_encoder_mother_(tailed_bits);

        // Step 4 — Puncturing
        std::vector<int> output = puncture_(mother_bits, coding_rate);

        return output;
    }

    std::vector<int> FECModule::data_scrambler_prbs(const std::size_t n, const std::uint8_t seed_7bit)
    {
        std::vector<int> regs(7);
        for (std::size_t i = 0; i < 7; ++i)
            regs[i] = static_cast<int>((seed_7bit >> i) & 1U);

        std::vector<int> bits(n);
        for (std::size_t i = 0; i < n; ++i)
            bits[i] = LFSRStep_(regs);
        return bits;
    }

    std::vector<int> FECModule::scramble_(std::vector<int> bits, const std::uint8_t seed_7bit)
    {
        std::vector<int> regs(7);
        for (std::size_t i = 0; i < 7; ++i) {
            regs[i] = static_cast<int>((seed_7bit >> i) & 1U);
        }

        std::vector<int> scrambled_bits;
        scrambled_bits.reserve(bits.size());
        for (const int bit : bits) {
            scrambled_bits.push_back(LFSRStep_(regs) ^ bit);
        }
        return scrambled_bits;
    }

    std::vector<int> FECModule::append_convolutional_tail_(std::vector<int> scrambled_bits)
    {
        constexpr int TAIL_BITS = 6;
        scrambled_bits.insert(scrambled_bits.end(), TAIL_BITS, 0);
        return scrambled_bits;
    }

    std::vector<int> FECModule::convolutional_encoder_mother_(const std::vector<int>& scrambled_bits)
    {
        std::vector<int> regs = {0, 0, 0, 0, 0, 0, 0};
        std::vector<int> output_bits;
        for (int bit : scrambled_bits)
        {
            for (int j = 0; j < 6; j++)
            {
                regs[j] = regs[j + 1];
            }
            regs[6] = bit;
            int A = regs[0] ^ regs[1] ^ regs[3] ^ regs[4] ^ regs[6];
            int B = regs[0] ^ regs[3] ^ regs[4] ^ regs[5] ^ regs[6];
            output_bits.push_back(A);
            output_bits.push_back(B);
        }
        return output_bits;
    }

    std::vector<int> FECModule::puncture_(std::vector<int> scrambled_bits, const CodingRates rate){
        if (rate == CodingRates::R12) return scrambled_bits;
        if (rate == CodingRates::R34)
        {
            // Pattern is  A: [1, 1, 0], B: [1, 0, 1]
            std::vector<int> output_bits;
            for (size_t i = 0; i < scrambled_bits.size(); i += 6)
            {
                output_bits.push_back(scrambled_bits[i]);
                output_bits.push_back(scrambled_bits[i+1]);
                output_bits.push_back(scrambled_bits[i+2]);
                output_bits.push_back(scrambled_bits[i+5]);
            }
            return output_bits;
        }
        // Pattern is A: [1, 1], B: [1, 0] assuming CodingRate is R23
        std::vector<int> output_bits;
        for (size_t i = 0; i < scrambled_bits.size(); i += 4)
        {
            output_bits.push_back(scrambled_bits[i]);
            output_bits.push_back(scrambled_bits[i+1]);
            output_bits.push_back(scrambled_bits[i+2]);
        }
        return output_bits;
    }

    int FECModule::LFSRStep_(std::vector<int>& regs)
    {
        const int feedback = regs[3] ^ regs[6];
        const int prbs_bit = feedback;

        for (std::size_t i = regs.size() - 1; i > 0; --i) {
            regs[i] = regs[i - 1];
        }
        regs[0] = feedback;

        return prbs_bit;
    }
}
