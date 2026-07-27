#include "phy/modulation_module.h"

#include "phy/helpers.h"
#include "phy/link_settings.h"

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <string_view>
#include <vector>

namespace {

// -- helpers ---------------------------------------------------------------

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

// -- golden data from IEEE 802.11a-1999 Annex G ----------------------------

/// Table G.9: SIGNAL field bits after interleaving (BPSK input to mapper).
constexpr std::string_view kSignalInterleaved =
    "100101001101000000010100100000110010010010010100";

/// Table G.21: interleaved bits of the first DATA symbol (16-QAM input to mapper).
constexpr std::string_view kDataSymbol1Interleaved =
    "0111011111110000111011111100010001110011000000001011111100010001000100001001"
    "1010000111010001001001101110001110001111010101101001000110110110101110011000"
    "0100001100000000000011011011001101101101";

/// Derived from Table G.10: the 48 BPSK symbols carried on the SIGNAL field's
/// data subcarriers, in increasing subcarrier order (k = -26..26, skipping
/// k=0 and the pilot subcarriers {-21,-7,7,21}) -- i.e. exactly the order
/// map_bits_to_constellation() and ofdm_module::modulate() both use.
constexpr std::array<std::complex<double>, 48> kSignalBpskGolden = {
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
    std::complex<double>{-1.000000000000, 0.000000000000},
};

/// Derived from Table G.22: the 48 16-QAM symbols carried on DATA symbol 1's
/// data subcarriers, same ordering convention as above. Cross-checked against
/// the standard's own worked example ("the first 4 bits (0 1 1 1) are mapped
/// to -0.316+0.316j, inserted at subcarrier #26" i.e. k=-26, element 0 below).
constexpr std::array<std::complex<double>, 48> kData1Qam16Golden = {
    std::complex<double>{-0.316227766017, 0.316227766017},
    std::complex<double>{-0.316227766017, 0.316227766017},
    std::complex<double>{0.316227766017, 0.316227766017},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{0.316227766017, 0.948683298051},
    std::complex<double>{0.316227766017, 0.316227766017},
    std::complex<double>{0.316227766017, -0.948683298051},
    std::complex<double>{-0.316227766017, -0.948683298051},
    std::complex<double>{-0.316227766017, 0.316227766017},
    std::complex<double>{-0.948683298051, 0.316227766017},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{0.948683298051, 0.316227766017},
    std::complex<double>{0.316227766017, 0.316227766017},
    std::complex<double>{-0.948683298051, -0.316227766017},
    std::complex<double>{-0.948683298051, -0.316227766017},
    std::complex<double>{-0.948683298051, -0.316227766017},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{0.948683298051, -0.316227766017},
    std::complex<double>{0.948683298051, 0.948683298051},
    std::complex<double>{-0.948683298051, -0.316227766017},
    std::complex<double>{0.316227766017, -0.316227766017},
    std::complex<double>{-0.948683298051, -0.316227766017},
    std::complex<double>{-0.948683298051, 0.948683298051},
    std::complex<double>{-0.316227766017, 0.948683298051},
    std::complex<double>{0.316227766017, 0.948683298051},
    std::complex<double>{-0.948683298051, 0.316227766017},
    std::complex<double>{0.948683298051, -0.948683298051},
    std::complex<double>{0.316227766017, 0.316227766017},
    std::complex<double>{-0.316227766017, -0.316227766017},
    std::complex<double>{-0.316227766017, 0.948683298051},
    std::complex<double>{0.948683298051, -0.316227766017},
    std::complex<double>{-0.948683298051, -0.316227766017},
    std::complex<double>{0.948683298051, 0.316227766017},
    std::complex<double>{-0.316227766017, 0.948683298051},
    std::complex<double>{0.948683298051, 0.316227766017},
    std::complex<double>{0.948683298051, -0.316227766017},
    std::complex<double>{0.948683298051, -0.948683298051},
    std::complex<double>{-0.316227766017, -0.948683298051},
    std::complex<double>{-0.948683298051, 0.316227766017},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{-0.948683298051, -0.948683298051},
    std::complex<double>{0.316227766017, -0.316227766017},
    std::complex<double>{0.948683298051, 0.316227766017},
    std::complex<double>{-0.948683298051, 0.316227766017},
    std::complex<double>{-0.316227766017, 0.948683298051},
    std::complex<double>{0.316227766017, -0.316227766017},
};

} // namespace

// -- individual tests -------------------------------------------------------

/// G.4.4: BPSK-mapping the interleaved SIGNAL field bits (Table G.9) must
/// reproduce the 48 data-subcarrier values of Table G.10.
TEST(Modulation, SignalFieldBpskMatchesAnnexG)
{
    static_assert(kSignalInterleaved.size() == 48);
    const auto syms = wifi80211a::map_bits_to_constellation(
        bits_from_sv(kSignalInterleaved), wifi80211a::ModulationTypes::BPSK, 1);
    ASSERT_EQ(syms.size(), kSignalBpskGolden.size());
    for (std::size_t i = 0; i < syms.size(); ++i)
        EXPECT_TRUE(wifi80211a::areClose(syms[i], kSignalBpskGolden[i], 1e-9)) << "subcarrier index " << i;
}

/// G.6.3: 16-QAM-mapping the interleaved DATA symbol 1 bits (Table G.21) must
/// reproduce the 48 data-subcarrier values of Table G.22.
TEST(Modulation, DataFirstSymbolQam16MatchesAnnexG)
{
    static_assert(kDataSymbol1Interleaved.size() == 192);
    const auto syms = wifi80211a::map_bits_to_constellation(
        bits_from_sv(kDataSymbol1Interleaved), wifi80211a::ModulationTypes::QAM16, 4);
    ASSERT_EQ(syms.size(), kData1Qam16Golden.size());
    for (std::size_t i = 0; i < syms.size(); ++i)
        EXPECT_TRUE(wifi80211a::areClose(syms[i], kData1Qam16Golden[i], 1e-9)) << "subcarrier index " << i;
}

/// map_constellation_to_bits() must exactly invert map_bits_to_constellation()
/// for a noise-free 16-QAM constellation (round-trip using the Annex G data).
TEST(Modulation, Qam16RoundTrip)
{
    const auto bits_in = bits_from_sv(kDataSymbol1Interleaved);
    const auto syms = wifi80211a::map_bits_to_constellation(bits_in, wifi80211a::ModulationTypes::QAM16, 4);
    const auto bits_out = wifi80211a::map_constellation_to_bits(syms, wifi80211a::ModulationTypes::QAM16, 4);
    EXPECT_EQ(bits_out, bits_in);
}

