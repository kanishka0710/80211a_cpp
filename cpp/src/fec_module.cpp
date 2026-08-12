//
// Created by Kanishka on 4/7/2026.
//

#include "phy/fec_module.h"

#include <cstddef>
#include <limits>
#include <algorithm>
#include <stdexcept>

namespace wifi80211a
{
    namespace
    {
        // Mother-code input length must be a multiple of this for the puncture
        // pattern to consume whole blocks (3 input bits -> 6 mother bits for
        // R3/4, 2 input bits -> 4 mother bits for R2/3).
        int input_period_for_puncture_(const CodingRates rate)
        {
            if (rate == CodingRates::R34) return 3;
            if (rate == CodingRates::R23) return 2;
            return 1;
        }
    }

    std::vector<int> performFEC(
        std::vector<int> data_bits,
        const CodingRates coding_rate,
        const std::uint8_t scrambler_seed_7bit)
    {

        // Step 1 — Scrambler
        std::vector<int> scrambled_bits = scramble_(data_bits, scrambler_seed_7bit);

        constexpr int kTailBits = 6;
        const int period = input_period_for_puncture_(coding_rate);
        const int remainder = (static_cast<int>(scrambled_bits.size()) + kTailBits) % period;
        const int pad = remainder == 0 ? 0 : period - remainder;
        if (pad > 0) {
            scrambled_bits.insert(scrambled_bits.end(), pad, 0);
        }

        // Step 2 — Convolutional tail
        std::vector<int> tailed_bits = append_convolutional_tail_(scrambled_bits);

        // Step 3 — Conv. encoder (mother 1/2)
        std::vector<int> mother_bits = convolutional_encoder_mother_(tailed_bits);

        // Step 4 — Puncturing
        std::vector<int> output = puncture_(mother_bits, coding_rate);

        return output;
    }

    std::vector<int> performFECRX(std::vector<int>& bits, const CodingRates coding_rate, std::uint8_t scrambler_seed_7bit) {
        auto [depunctured, mask_bits] = depuncture_(bits, coding_rate);
        auto decoded = viterbi_decode_(depunctured, mask_bits);
        return scramble_(decoded, scrambler_seed_7bit);
    }

    int compute_ndbps(const int n_cbps, const CodingRates coding_rate)
    {
        switch (coding_rate) {
            case CodingRates::R12: return n_cbps / 2;
            case CodingRates::R23: return n_cbps * 2 / 3;
            case CodingRates::R34: return n_cbps * 3 / 4;
        }
        throw std::invalid_argument("compute_ndbps: unhandled CodingRates value");
    }

    std::pair<int, int> compute_data_field_sizing(const int psdu_len_bits, const int n_dbps)
    {
        const int n_msg_tail = kServiceBits + psdu_len_bits + kDataTailBits;
        const int n_sym = (n_msg_tail + n_dbps - 1) / n_dbps;
        const int n_data = n_sym * n_dbps;
        const int n_pad = n_data - n_msg_tail;
        return {n_sym, n_pad};
    }

    std::vector<int> performFECDataField(
        std::vector<int> psdu_bits,
        const CodingRates coding_rate,
        const std::uint8_t scrambler_seed_7bit,
        const int n_dbps)
    {
        const auto [n_sym, n_pad] = compute_data_field_sizing(static_cast<int>(psdu_bits.size()), n_dbps);
        (void)n_sym;

        std::vector<int> unscrambled;
        unscrambled.reserve(kServiceBits + psdu_bits.size() + kDataTailBits + n_pad);
        unscrambled.insert(unscrambled.end(), kServiceBits, 0);
        unscrambled.insert(unscrambled.end(), psdu_bits.begin(), psdu_bits.end());
        unscrambled.insert(unscrambled.end(), kDataTailBits + n_pad, 0);

        std::vector<int> scrambled_bits = scramble_(unscrambled, scrambler_seed_7bit);

        const std::size_t tail_start = static_cast<std::size_t>(kServiceBits) + psdu_bits.size();
        for (int i = 0; i < kDataTailBits; ++i) {
            scrambled_bits[tail_start + i] = 0;
        }

        std::vector<int> mother_bits = convolutional_encoder_mother_(scrambled_bits);
        return puncture_(mother_bits, coding_rate);
    }

    std::vector<int> performFECDataFieldRX(
        std::vector<int>& bits,
        const CodingRates coding_rate,
        const std::uint8_t scrambler_seed_7bit)
    {
        auto [depunctured, mask_bits] = depuncture_(bits, coding_rate);
        auto decoded = viterbi_decode_(depunctured, mask_bits);
        auto descrambled = scramble_(decoded, scrambler_seed_7bit);
        return std::vector<int>(descrambled.begin() + kServiceBits, descrambled.end());
    }


    std::vector<int> data_scrambler_prbs(const std::size_t n, const std::uint8_t seed_7bit)
    {
        std::vector<int> regs(7);
        for (std::size_t i = 0; i < 7; ++i)
            regs[i] = static_cast<int>((seed_7bit >> i) & 1U);

        std::vector<int> bits(n);
        for (std::size_t i = 0; i < n; ++i)
            bits[i] = LFSRStep_(regs);
        return bits;
    }

    std::vector<int> scramble_(std::vector<int> bits, const std::uint8_t seed_7bit)
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

    std::vector<int> append_convolutional_tail_(std::vector<int> scrambled_bits)
    {
        constexpr int TAIL_BITS = 6;
        scrambled_bits.insert(scrambled_bits.end(), TAIL_BITS, 0);
        return scrambled_bits;
    }

    std::vector<int> convolutional_encoder_mother_(const std::vector<int>& scrambled_bits)
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

    std::vector<int> puncture_(std::vector<int> scrambled_bits, const CodingRates rate){
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

    std::tuple<std::vector<int>, std::vector<int>> depuncture_(std::vector<int> rx_bits, const CodingRates rate) {

        std::vector<int> mask_bits;
        if (rate == CodingRates::R12) {
            mask_bits = std::vector<int>(rx_bits.size(), 1);
            return std::make_tuple(rx_bits, mask_bits);
        }
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
                mask_bits.insert(mask_bits.end(), {1, 1, 1, 0, 0, 1});
            }
            return std::make_tuple(output_bits, mask_bits);
        }
        if (rate == CodingRates::R23)
        {
            std::vector<int> output_bits;
            for (size_t i = 0; i < rx_bits.size(); i += 3)
            {
                output_bits.push_back(rx_bits[i]);
                output_bits.push_back(rx_bits[i+1]);
                output_bits.push_back(rx_bits[i+2]);
                output_bits.push_back(0);
                mask_bits.insert(mask_bits.end(), {1, 1, 1, 0});
            }
            return std::make_tuple(output_bits, mask_bits);
        }
        throw std::invalid_argument("depuncture_: unhandled CodingRates value");
    }

    int LFSRStep_(std::vector<int>& regs)
    {
        const int feedback = regs[3] ^ regs[6];
        const int prbs_bit = feedback;

        for (std::size_t i = regs.size() - 1; i > 0; --i) {
            regs[i] = regs[i - 1];
        }
        regs[0] = feedback;

        return prbs_bit;
    }

    std::vector<int> viterbi_decode_(std::vector<int> rx_bits, std::vector<int> mask_bits) {
        auto [trellis_A, trellis_B, next_states] = precompute_trellis_();
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
                    int branchMetric = (rx_A ^ trellis_A[s][u]) * mask_bits[2*t] + (rx_B ^ trellis_B[s][u]) * mask_bits[2*t+1];
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
        for (int s = 1; s < num_states; s++) {
            if (pathMetrics[s] < pathMetrics[state]) state = s;
        }
        std::vector<int> decoded_bits(T);
        for (int t = T-1; t >= 0; t--) {
            int prevState = pathHistory[t][state];
            decoded_bits[t] = (state >> 5) & 1;
            state = prevState;
        }
        return decoded_bits;
    }

    std::tuple<std::vector<std::vector<int>>, std::vector<std::vector<int>>, std::vector<std::vector<int>>> precompute_trellis_() {
        std::vector<std::vector<int>> trellis_A(64, std::vector<int>(2, 0));
        std::vector<std::vector<int>> trellis_B(64, std::vector<int>(2, 0));
        std::vector<std::vector<int>> next_states(64, std::vector<int>(2, 0));

        for (int s = 0; s < 64; s++) {
            for (int i = 0; i < 2; i++) {
                next_states[s][i] = (s >> 1) | (i << 5);
                trellis_A[s][i]   = (s&1) ^ ((s>>1)&1) ^ ((s>>3)&1) ^ ((s>>4)&1) ^ i;
                trellis_B[s][i]   = (s&1) ^ ((s>>3)&1) ^ ((s>>4)&1) ^ ((s>>5)&1) ^ i;
            }
        }
        return std::make_tuple(trellis_A, trellis_B, next_states);
    }
}
