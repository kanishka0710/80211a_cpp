#include "phy/interleaver_module.h"

#include <gtest/gtest.h>

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

/// Table G.8: SIGNAL field bits after rate-1/2 encoding (before interleaving).
constexpr std::string_view kSignalCoded =
    "110100011010000100000010001111100111000000000000";

/// Table G.9: SIGNAL field bits after interleaving (n_cbps=48, n_bpsc=1, BPSK).
constexpr std::string_view kSignalInterleaved =
    "100101001101000000010100100000110010010010010100";

/// Table G.18: coded bits of the first DATA symbol (36 Mb/s: 16-QAM, R=3/4).
constexpr std::string_view kDataSymbol1Coded =
    "001010110000100010100001111100001001110110110101100110100001110101001010"
    "111110111110100011000010100011111100000011001000011100111100000001000011"
    "111000000001100111100000110100111110101110110010";

/// Table G.21: interleaved bits of the first DATA symbol (n_cbps=192, n_bpsc=4).
constexpr std::string_view kDataSymbol1Interleaved =
    "011101111111000011101111110001000111001100000000101111110001000100010000"
    "100110100001110100010010011011100011100011110101011010010001101101101011"
    "100110000100001100000000000011011011001101101101";

} // namespace

// ── individual tests ──────────────────────────────────────────────────────────

/// 17.3.5.6 / G.4.3: SIGNAL field interleaving (n_cbps=48, n_bpsc=1) must
/// reproduce Table G.9 from Table G.8.
TEST(Interleaver, SignalFieldMatchesAnnexG)
{
    static_assert(kSignalCoded.size() == 48);
    static_assert(kSignalInterleaved.size() == 48);
    const auto got = wifi80211a::interleave(bits_from_sv(kSignalCoded), 48, 1);
    EXPECT_EQ(got, bits_from_sv(kSignalInterleaved));
}

/// deinterleave() must exactly invert interleave() for the SIGNAL field.
TEST(Interleaver, SignalFieldRoundTrip)
{
    const auto coded = bits_from_sv(kSignalCoded);
    const auto interleaved = wifi80211a::interleave(coded, 48, 1);
    const auto back = wifi80211a::deinterleave(interleaved, 48, 1);
    EXPECT_EQ(back, coded);
}

/// 17.3.5.6 / G.6.2: DATA field interleaving for 16-QAM R=3/4 (n_cbps=192,
/// n_bpsc=4) must reproduce Table G.21 from Table G.18.
TEST(Interleaver, DataFirstSymbolMatchesAnnexG)
{
    static_assert(kDataSymbol1Coded.size() == 192);
    static_assert(kDataSymbol1Interleaved.size() == 192);
    const auto got = wifi80211a::interleave(bits_from_sv(kDataSymbol1Coded), 192, 4);
    EXPECT_EQ(got, bits_from_sv(kDataSymbol1Interleaved));
}

/// deinterleave() must exactly invert interleave() for the 16-QAM DATA field.
TEST(Interleaver, DataFirstSymbolRoundTrip)
{
    const auto coded = bits_from_sv(kDataSymbol1Coded);
    const auto interleaved = wifi80211a::interleave(coded, 192, 4);
    const auto back = wifi80211a::deinterleave(interleaved, 192, 4);
    EXPECT_EQ(back, coded);
}
