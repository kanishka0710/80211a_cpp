#include "phy/fec_module.h"

#include <gtest/gtest.h>

#include <cstdint>
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

/// Table G.13: first 144 bits of the (unscrambled) 36 Mb/s DATA field, i.e.
/// SERVICE field + start of the PSDU from the Annex G worked example.
constexpr std::string_view kDataFirst144Unscrambled =
    "000000000000000000100000010000000000000001110100000000000000011000010"
    "000101100111110110001100101000000000000010001101011100000000011110010"
    "001111";

/// Table G.16: the same 144 bits after scrambling with seed 1011101 (0x5D).
constexpr std::string_view kDataFirst144Scrambled =
    "011011000001100110001001100011110110100000100001111101001010010101100"
    "001010011111101011110101110001001000000110011110011001110101110010010"
    "111100";

/// Table G.18: the 192 bits produced by rate-3/4 encoding of the first 144
/// scrambled DATA bits (36 Mb/s uses 16-QAM, R=3/4: 144 in -> 192 out).
constexpr std::string_view kDataSymbol1Coded =
    "001010110000100010100001111100001001110110110101100110100001110101001"
    "010111110111110100011000010100011111100000011001000011100111100000001"
    "000011111000000001100111100000110100111110101110110010";

} // namespace

// ── individual tests ──────────────────────────────────────────────────────────

/// 17.3.5.4: verify the scrambler PRBS against the standard's all-ones seed sequence.
TEST(ForwardErrorCorrection, ScramblerAllOnesSeed)
{
    static_assert(kScramblerGolden_0x7F.size() == 127);
    const auto got = wifi80211a::data_scrambler_prbs(127, 0x7F);
    EXPECT_EQ(got, bits_from_sv(kScramblerGolden_0x7F));
}

/// Table G.15: verify the scrambler against the seed used in the Annex G worked example.
TEST(ForwardErrorCorrection, ScramblerAnnexG52Seed)
{
    static_assert(kScramblerGolden_0x5D.size() == 127);
    const auto got = wifi80211a::data_scrambler_prbs(127, 0x5D);
    EXPECT_EQ(got, bits_from_sv(kScramblerGolden_0x5D));
}

/// Tables G.7 → G.8: verify the full encode pipeline against the SIGNAL field example.
/// seed=0 makes the scrambler a no-op (all-zero LFSR XORs with 0), so only the
/// convolutional encoder and tail are exercised.
TEST(ForwardErrorCorrection, EncoderSignalField)
{
    static_assert(kSignalFieldData.size()    == 18);
    static_assert(kSignalFieldEncoded.size() == 48);
    const auto got = wifi80211a::performFEC(bits_from_sv(kSignalFieldData),
                                    wifi80211a::CodingRates::R12,
                                    /*scrambler_seed_7bit=*/0);
    EXPECT_EQ(got, bits_from_sv(kSignalFieldEncoded));
}

/// Verify the tail zeros flush the encoder: an all-zero input with a zero seed keeps
/// the K=7 shift register at the all-zero state throughout, so every output bit is 0.
TEST(ForwardErrorCorrection, TailFlushesEncoder)
{
    // Empty input → scrambler produces nothing → append_tail adds 6 zeros →
    // encoder sees 6 zeros → shift register stays at zero → 12 zero output bits.
    const auto got = wifi80211a::performFEC({}, wifi80211a::CodingRates::R12, 0);
    const std::vector<int> expected(12, 0);
    EXPECT_EQ(got, expected);
}

/// Rate 1/2: output length must be 2 * (N_input + 6) with no puncturing.
TEST(ForwardErrorCorrection, OutputLengthR12)
{
    constexpr std::size_t N = 20;
    const auto got = wifi80211a::performFEC(std::vector<int>(N, 0),
                                    wifi80211a::CodingRates::R12, 0);
    EXPECT_EQ(got.size(), 2 * (N + 6));
}

/// Rate 2/3: for every 4 mother bits 3 are kept.
/// N=2 → 2*(2+6)=16 mother bits → 4 groups of 4 → 12 output bits.
TEST(ForwardErrorCorrection, OutputLengthR23)
{
    constexpr std::size_t N = 2;
    const auto got = wifi80211a::performFEC(std::vector<int>(N, 0),
                                    wifi80211a::CodingRates::R23, 0);
    EXPECT_EQ(got.size(), 12u);
}

/// Rate 3/4: for every 6 mother bits 4 are kept.
/// N=0 → 2*(0+6)=12 mother bits → 2 groups of 6 → 8 output bits.
TEST(ForwardErrorCorrection, OutputLengthR34)
{
    const auto got = wifi80211a::performFEC({}, wifi80211a::CodingRates::R34, 0);
    EXPECT_EQ(got.size(), 8u);
}

/// Table G.13 → G.16: the scrambler is a pure position-keyed PRBS (its output
/// does not depend on the data being scrambled), so XOR-ing the first 144
/// unscrambled DATA bits with a freshly-seeded 144-bit PRBS run must
/// reproduce the first 144 scrambled bits from the Annex G worked example.
TEST(ForwardErrorCorrection, DataFieldScramblingMatchesAnnexG)
{
    static_assert(kDataFirst144Unscrambled.size() == 144);
    static_assert(kDataFirst144Scrambled.size()   == 144);
    const auto got = wifi80211a::scramble_(bits_from_sv(kDataFirst144Unscrambled), 0x5D);
    EXPECT_EQ(got, bits_from_sv(kDataFirst144Scrambled));
}

/// Table G.16 → G.18: mother-rate-1/2 encoding the first 144 scrambled DATA
/// bits followed by rate-3/4 puncturing must reproduce the 192 coded bits of
/// the first DATA symbol. This is tested at the encoder_/puncture_ level
/// (rather than via performFEC) because performFEC always appends its own
/// 6-bit convolutional tail, whereas the true tail here only follows bit 863
/// of the 864-bit DATA field -- far beyond this 144-bit prefix.
TEST(ForwardErrorCorrection, DataFirstSymbolPunctureR34MatchesAnnexG)
{
    static_assert(kDataSymbol1Coded.size() == 192);
    const auto mother = wifi80211a::convolutional_encoder_mother_(bits_from_sv(kDataFirst144Scrambled));
    const auto got = wifi80211a::puncture_(mother, wifi80211a::CodingRates::R34);
    EXPECT_EQ(got, bits_from_sv(kDataSymbol1Coded));
}
