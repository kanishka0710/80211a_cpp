//
// Created by Kanishka on 4/7/2026.
//

#include "phy/fec_module.h"

#include <cstddef>
#include <limits>
#include <algorithm>

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

    std::vector<int> FECModule::performFECRX(std::vector<int>& bits, const CodingRates coding_rate, std::uint8_t scrambler_seed_7bit) {
        auto depunctured = depuncture_(bits, coding_rate);
        auto decoded = viterbi_decode_(depunctured);
        return scramble_(decoded, scrambler_seed_7bit);
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

    std::vector<int> FECModule::depuncture_(std::vector<int> rx_bits, const CodingRates rate) {
        if (rate == CodingRates::R12) return rx_bits;
        if (rate == CodingRates::R34) {
            std::vector<int> output_bits;
            for (size_t i = 0; i < rx_bits.size(); i += 4)
            {
                output_bits.push_back(rx_bits[i]);
                output_bits.push_back(rx_bits[i+1]);
                output_bits.push_back(rx_bits[i+2]);
                output_bits.push_back(0);
                output_bits.push_back(0);
                output_bits.push_back(rx_bits[i+3]);
            }
            return output_bits;
        }
        if (rate == CodingRates::R23) {
            std::vector<int> output_bits;
            for (size_t i = 0; i < rx_bits.size(); i += 3)
            {
                output_bits.push_back(rx_bits[i]);
                output_bits.push_back(rx_bits[i+1]);
                output_bits.push_back(rx_bits[i+2]);
                output_bits.push_back(0);
            }
            return output_bits;
        }
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

    std::vector<int> FECModule::viterbi_decode_(std::vector<int> rx_bits) {
        precompute_trellis_();
        constexpr int num_states = 64;
        int T = (int)rx_bits.size() / 2;
        std::vector<int> pathMetrics(num_states, std::numeric_limits<int>::max());
        std::vector<std::vector<int>> pathHistory(T, std::vector<int>(num_states, 0));
        pathMetrics[0] = 0;

        for (int t = 0; t < T; t++) {
            auto rx_A = rx_bits[2*t];
            auto rx_B = rx_bits[2*t+1];

            std::vector<int> newMetric(num_states, std::numeric_limits<int>::max());
            for (int s = 0; s < num_states; s++) {
                if (pathMetrics[s] == std::numeric_limits<int>::max()) continue;
                for (int u = 0; u < 2; u++) {
                    int branchMetric = (rx_A ^ trellis_A[s][u]) + (rx_B ^ trellis_B[s][u]);
                    int ns = next_states[s][u];
                    int candidate = pathMetrics[s] + branchMetric;
                    if (candidate < newMetric[ns]) {
                        newMetric[ns] = candidate;
                        pathHistory[t][ns] = s;
                    }
                }
            }
            pathMetrics = newMetric;
        }

        int state = 0;
        std::vector<int> decoded_bits(T);
        for (int t = T-1; t >= 0; t--) {
            int prevState = pathHistory[t][state];
            decoded_bits[t] = (state >> 5) & 1;
            state = prevState;
        }
        return decoded_bits;
    }

    void FECModule::precompute_trellis_() {
        if (trellis_ready_) return;
        for (int s = 0; s < 64; s++) {
            for (int i = 0; i < 2; i++) {
                next_states[s][i] = (s >> 1) | (i << 5);
                trellis_A[s][i]   = (s&1) ^ ((s>>1)&1) ^ ((s>>3)&1) ^ ((s>>4)&1) ^ i;
                trellis_B[s][i]   = (s&1) ^ ((s>>3)&1) ^ ((s>>4)&1) ^ ((s>>5)&1) ^ i;
            }
        }
        trellis_ready_ = true;
    }
}
