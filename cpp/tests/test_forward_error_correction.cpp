#include "phy/fec_module.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

// ── helpers ───────────────────────────────────────────────────────────────────

std::vector<int> bits_from_sv(std::string_view sv)
{
    std::vector<int> out;
    out.reserve(sv.size());
    for (const char c : sv) {
        if      (c == '0') out.push_back(0);
        else if (c == '1') out.push_back(1);
    }
    return out;
}

bool check_equal(std::string_view label,
                 const std::vector<int>& got,
                 const std::vector<int>& expected)
{
    if (got.size() != expected.size()) {
        std::cerr << "[FAIL] " << label << ": size mismatch — got "
                  << got.size() << ", expected " << expected.size() << '\n';
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (got[i] != expected[i]) {
            std::cerr << "[FAIL] " << label << ": mismatch at bit " << i
                      << " — got " << got[i] << ", expected " << expected[i] << '\n';
            return false;
        }
    }
    std::cout << "[ OK ] " << label << '\n';
    return true;
}

bool check_size(std::string_view label, std::size_t got, std::size_t expected)
{
    if (got != expected) {
        std::cerr << "[FAIL] " << label << ": size " << got
                  << ", expected " << expected << '\n';
        return false;
    }
    std::cout << "[ OK ] " << label << " (" << got << " bits)\n";
    return true;
}

// ── golden data from IEEE 802.11a-1999 Annex G ────────────────────────────────

/// 17.3.5.4: first 127 PRBS bits, scrambler seed = all-ones (0x7F).
constexpr std::string_view kScramblerGolden_0x7F =
    "0000111011110010110010010000001000100110001011101011011000001100"
    "110101001110011110110100001010101111101001010001101110001111111";

/// Table G.15: first 127 PRBS bits, scrambler seed = 1011101₂ = 0x5D (Annex G.5.2).
constexpr std::string_view kScramblerGolden_0x5D =
    "0110110000011001101010011100111101101000010101011111010010100011"
    "011100011111110000111011110010110010010000001000100110001011101";

/// Table G.7 bits 0–17: the 18 SIGNAL field data bits (before the 6 SIGNAL TAIL zeros).
/// The SIGNAL field is never scrambled, so feeding these into performFEC with seed=0
/// is equivalent to presenting the full 24-bit SIGNAL field input to the encoder.
constexpr std::string_view kSignalFieldData =
    "101100010011000000";

/// Table G.8: 48 bits produced by rate-1/2 encoding of the 24-bit SIGNAL field
/// (18 data bits + 6 SIGNAL TAIL zeros).  The final 12 bits are all zero because
/// the tail flushes the K=7 shift register back to the all-zero state.
constexpr std::string_view kSignalFieldEncoded =
    "110100011010000100000010001111100111000000000000";

} // namespace

// ── individual tests ──────────────────────────────────────────────────────────

/// 17.3.5.4: verify the scrambler PRBS against the standard's all-ones seed sequence.
static bool test_scrambler_all_ones_seed()
{
    static_assert(kScramblerGolden_0x7F.size() == 127);
    const auto got = wifi80211a::data_scrambler_prbs(127, 0x7F);
    return check_equal("Scrambler: seed=0x7F vs IEEE 802.11 127-bit PRBS golden",
                       got, bits_from_sv(kScramblerGolden_0x7F));
}

/// Table G.15: verify the scrambler against the seed used in the Annex G worked example.
static bool test_scrambler_g61_seed()
{
    static_assert(kScramblerGolden_0x5D.size() == 127);
    const auto got = wifi80211a::data_scrambler_prbs(127, 0x5D);
    return check_equal("Scrambler: seed=0x5D (Annex G.5.2) vs Table G.15",
                       got, bits_from_sv(kScramblerGolden_0x5D));
}

/// Tables G.7 → G.8: verify the full encode pipeline against the SIGNAL field example.
/// seed=0 makes the scrambler a no-op (all-zero LFSR XORs with 0), so only the
/// convolutional encoder and tail are exercised.
static bool test_encoder_signal_field()
{
    static_assert(kSignalFieldData.size()    == 18);
    static_assert(kSignalFieldEncoded.size() == 48);
    const auto got = wifi80211a::performFEC(bits_from_sv(kSignalFieldData),
                                    wifi80211a::CodingRates::R12,
                                    /*scrambler_seed_7bit=*/0);
    return check_equal("Encoder (R1/2, seed=0): Table G.7 bits 0–17 → Table G.8",
                       got, bits_from_sv(kSignalFieldEncoded));
}

/// Verify the tail zeros flush the encoder: an all-zero input with a zero seed keeps
/// the K=7 shift register at the all-zero state throughout, so every output bit is 0.
static bool test_tail_flushes_encoder()
{
    // Empty input → scrambler produces nothing → append_tail adds 6 zeros →
    // encoder sees 6 zeros → shift register stays at zero → 12 zero output bits.
    const auto got = wifi80211a::performFEC({}, wifi80211a::CodingRates::R12, 0);
    const std::vector<int> expected(12, 0);
    return check_equal("Tail: empty input + seed=0 → 12 zero-coded bits",
                       got, expected);
}

/// Rate 1/2: output length must be 2 * (N_input + 6) with no puncturing.
static bool test_output_length_r12()
{
    constexpr std::size_t N = 20;
    const auto got = wifi80211a::performFEC(std::vector<int>(N, 0),
                                    wifi80211a::CodingRates::R12, 0);
    return check_size("Output length R1/2 (N=20)", got.size(), 2 * (N + 6));
}

/// Rate 2/3: for every 4 mother bits 3 are kept.
/// N=2 → 2*(2+6)=16 mother bits → 4 groups of 4 → 12 output bits.
static bool test_output_length_r23()
{
    constexpr std::size_t N = 2;
    const auto got = wifi80211a::performFEC(std::vector<int>(N, 0),
                                    wifi80211a::CodingRates::R23, 0);
    return check_size("Output length R2/3 (N=2)", got.size(), 12);
}

/// Rate 3/4: for every 6 mother bits 4 are kept.
/// N=0 → 2*(0+6)=12 mother bits → 2 groups of 6 → 8 output bits.
static bool test_output_length_r34()
{
    const auto got = wifi80211a::performFEC({}, wifi80211a::CodingRates::R34, 0);
    return check_size("Output length R3/4 (N=0)", got.size(), 8);
}

// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    bool all_passed = true;
    all_passed &= test_scrambler_all_ones_seed();
    all_passed &= test_scrambler_g61_seed();
    all_passed &= test_encoder_signal_field();
    all_passed &= test_tail_flushes_encoder();
    all_passed &= test_output_length_r12();
    all_passed &= test_output_length_r23();
    all_passed &= test_output_length_r34();

    if (all_passed)
        std::cout << "\nAll tests passed.\n";
    else
        std::cerr << "\nOne or more tests FAILED.\n";

    return all_passed ? 0 : 1;
}
