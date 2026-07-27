#include "phy/signal_module.h"

#include "phy/fec_module.h"
#include "phy/helpers.h"
#include "phy/interleaver_module.h"
#include "phy/link_settings.h"
#include "phy/modulation_module.h"
#include "phy/ofdm_module.h"
#include "phy/pilot_utils.h"

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <stdexcept>
#include <string_view>
#include <utility>
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

/// Table G.7 bits 0-17: the 18 SIGNAL field data bits (RATE=1011 -> 36 Mb/s,
/// Reserved=0, LENGTH=100 octets LSB-first, Parity=0), before the 6 SIGNAL
/// TAIL zeros. Same constant independently cross-checked against Table G.8 by
/// test_forward_error_correction.cpp's EncoderSignalField test.
constexpr std::string_view kSignalFieldData = "101100010011000000";

/// Table G.9: SIGNAL field bits after interleaving (n_cbps=48, n_bpsc=1,
/// BPSK) -- the input to the BPSK mapper / OFDM modulator (G.4.4/G.4.5).
constexpr std::string_view kSignalInterleaved =
    "100101001101000000010100100000110010010010010100";

/// G.4.5: raw (unwindowed) IFFT of Table G.11 (SIGNAL field frequency domain
/// with pilots inserted), cyclically extended to [CP (last 16 of the 64-point
/// body) | body (64)] = 80 samples -- exactly what modulate() produces for a
/// single BPSK OFDM symbol. Independently recomputed via IFFT of Table G.11
/// and cross-checked sample-for-sample against Table G.12: every entry here
/// equals the corresponding Table G.12 entry, except sample 0, which Annex G
/// windows by 0.5 to smooth the overlap-and-add join with the LTF (G.3.1/
/// G.4.5 both note this windowing "is not part of the normative
/// specifications"; this codebase's modulate() does not implement it, so the
/// value below is the raw un-windowed sample -- the same convention used for
/// the STF/LTF golden vectors in test_preamble.cpp).
constexpr std::array<std::complex<double>, 80> kSignalOfdmSymbolGolden = {
    std::complex<double>{0.062500000000, 0.000000000000},
    std::complex<double>{0.033033759039, -0.043852698493},
    std::complex<double>{-0.001965565700, -0.037627011949},
    std::complex<double>{-0.080904659110, 0.084430523996},
    std::complex<double>{0.006774148479, -0.100151213260},
    std::complex<double>{-0.001246324746, -0.113302503192},
    std::complex<double>{-0.021146563289, -0.004640198548},
    std::complex<double>{0.135693094465, -0.104690379288},
    std::complex<double>{0.097541260736, -0.044194173824},
    std::complex<double>{0.011207482332, -0.001832582634},
    std::complex<double>{-0.032704455488, 0.044079978394},
    std::complex<double>{-0.060483306184, 0.124226874545},
    std::complex<double>{0.010138229651, 0.096601930309},
    std::complex<double>{0.000441056587, -0.007769538798},
    std::complex<double>{0.018360098248, -0.082503687234},
    std::complex<double>{-0.069283765261, 0.026733518639},
    std::complex<double>{-0.218750000000, 0.000000000000},
    std::complex<double>{-0.069283765261, -0.026733518639},
    std::complex<double>{0.018360098248, 0.082503687234},
    std::complex<double>{0.000441056587, 0.007769538798},
    std::complex<double>{0.010138229651, -0.096601930309},
    std::complex<double>{-0.060483306184, -0.124226874545},
    std::complex<double>{-0.032704455488, -0.044079978394},
    std::complex<double>{0.011207482332, 0.001832582634},
    std::complex<double>{0.097541260736, 0.044194173824},
    std::complex<double>{0.135693094465, 0.104690379288},
    std::complex<double>{-0.021146563289, 0.004640198548},
    std::complex<double>{-0.001246324746, 0.113302503192},
    std::complex<double>{0.006774148479, 0.100151213260},
    std::complex<double>{-0.080904659110, -0.084430523996},
    std::complex<double>{-0.001965565700, 0.037627011949},
    std::complex<double>{0.033033759039, 0.043852698493},
    std::complex<double>{0.062500000000, 0.000000000000},
    std::complex<double>{0.057212840537, 0.052497334937},
    std::complex<double>{0.015513862658, 0.173850788643},
    std::complex<double>{0.035461570205, 0.115563758552},
    std::complex<double>{-0.050968322303, -0.201625482037},
    std::complex<double>{0.010781243933, 0.035906380695},
    std::complex<double>{0.089258451636, 0.208513487760},
    std::complex<double>{-0.048513767352, -0.007887958693},
    std::complex<double>{-0.035041260736, 0.044194173824},
    std::complex<double>{0.017098132119, -0.058969060050},
    std::complex<double>{0.052980914789, -0.016983384478},
    std::complex<double>{0.098783816043, 0.100153698255},
    std::complex<double>{0.034055944173, -0.148378625605},
    std::complex<double>{-0.002833396727, -0.094012873951},
    std::complex<double>{-0.120296742854, 0.041950768631},
    std::complex<double>{-0.136447775879, -0.069865577492},
    std::complex<double>{-0.031250000000, 0.000000000000},
    std::complex<double>{-0.136447775879, 0.069865577492},
    std::complex<double>{-0.120296742854, -0.041950768631},
    std::complex<double>{-0.002833396727, 0.094012873951},
    std::complex<double>{0.034055944173, 0.148378625605},
    std::complex<double>{0.098783816043, -0.100153698255},
    std::complex<double>{0.052980914789, 0.016983384478},
    std::complex<double>{0.017098132119, 0.058969060050},
    std::complex<double>{-0.035041260736, -0.044194173824},
    std::complex<double>{-0.048513767352, 0.007887958693},
    std::complex<double>{0.089258451636, -0.208513487760},
    std::complex<double>{0.010781243933, -0.035906380695},
    std::complex<double>{-0.050968322303, 0.201625482037},
    std::complex<double>{0.035461570205, -0.115563758552},
    std::complex<double>{0.015513862658, -0.173850788643},
    std::complex<double>{0.057212840537, -0.052497334937},
    std::complex<double>{0.062500000000, 0.000000000000},
    std::complex<double>{0.033033759039, -0.043852698493},
    std::complex<double>{-0.001965565700, -0.037627011949},
    std::complex<double>{-0.080904659110, 0.084430523996},
    std::complex<double>{0.006774148479, -0.100151213260},
    std::complex<double>{-0.001246324746, -0.113302503192},
    std::complex<double>{-0.021146563289, -0.004640198548},
    std::complex<double>{0.135693094465, -0.104690379288},
    std::complex<double>{0.097541260736, -0.044194173824},
    std::complex<double>{0.011207482332, -0.001832582634},
    std::complex<double>{-0.032704455488, 0.044079978394},
    std::complex<double>{-0.060483306184, 0.124226874545},
    std::complex<double>{0.010138229651, 0.096601930309},
    std::complex<double>{0.000441056587, -0.007769538798},
    std::complex<double>{0.018360098248, -0.082503687234},
    std::complex<double>{-0.069283765261, 0.026733518639},
};

} // namespace

// ── individual tests ──────────────────────────────────────────────────────────

/// G.4.1: the 18 transmitted SIGNAL field bits are RATE(4b) + Reserved(1b) +
/// LENGTH(12b, LSB first) + Parity(1b) per 17.3.4. This verifies int_to_bits()
/// -- the primitive create_signal_header() uses to pack LENGTH -- reproduces
/// Annex G's worked example of a 100-octet PSDU (see G.5.1: "100 octets").
TEST(Signal, LengthFieldEncodingMatchesAnnexG)
{
    const auto bits = bits_from_sv(kSignalFieldData);
    ASSERT_EQ(bits.size(), 18u);

    const std::vector<int> lengthBits(bits.begin() + 5, bits.begin() + 17);
    EXPECT_EQ(lengthBits, wifi80211a::int_to_bits(100, 12));
}

/// G.4.4/G.4.5: BPSK-mapping the interleaved SIGNAL field bits (Table G.9)
/// and OFDM-modulating them (pilot insertion + IFFT + cyclic prefix, exactly
/// the path create_signal_header() takes via modulate()) must reproduce the
/// 80-sample SIGNAL field time-domain waveform of Table G.12.
TEST(Signal, OfdmWaveformMatchesAnnexG)
{
    static_assert(kSignalInterleaved.size() == 48);
    const auto symbols = wifi80211a::map_bits_to_constellation(
        bits_from_sv(kSignalInterleaved), wifi80211a::ModulationTypes::BPSK, 1);

    const wifi80211a::LinkSettings link_settings;
    const auto waveform = wifi80211a::modulate(link_settings, symbols, wifi80211a::PilotLFSR());

    ASSERT_EQ(waveform.size(), kSignalOfdmSymbolGolden.size());
    for (std::size_t i = 0; i < waveform.size(); ++i)
        EXPECT_TRUE(wifi80211a::areClose(waveform[i], kSignalOfdmSymbolGolden[i], 1e-9)) << "sample " << i;
}

/// 17.3.4.1 (Table 78): the SIGNAL field's RATE codeword is a fixed,
/// non-sequential 4-bit code per rate, not a binary index into rateMap.
/// Verifies rateCodeword reproduces every row of the standard's RATE table,
/// including Annex G's own 36 Mb/s example (Table G.7 bits 0-3 = 1,0,1,1).
TEST(Signal, RateCodewordMatchesStandardTable)
{
    using wifi80211a::CodingRates;
    using wifi80211a::ModulationTypes;

    // {rateValue, expected R1 R2 R3 R4}
    const std::vector<std::pair<int, std::string_view>> expected = {
        {1, "1101"}, // 6 Mb/s:  BPSK  R=1/2
        {2, "1111"}, // 9 Mb/s:  BPSK  R=3/4
        {3, "0101"}, // 12 Mb/s: QPSK  R=1/2
        {4, "0111"}, // 18 Mb/s: QPSK  R=3/4
        {5, "1001"}, // 24 Mb/s: 16QAM R=1/2
        {6, "1011"}, // 36 Mb/s: 16QAM R=3/4 (Annex G Table G.7 example)
        {7, "0001"}, // 48 Mb/s: 64QAM R=2/3
        {8, "0011"}, // 54 Mb/s: 64QAM R=3/4
    };
    for (const auto& [rateValue, r1234] : expected)
        EXPECT_EQ(wifi80211a::int_to_bits(wifi80211a::rateCodeword.at(rateValue), 4), bits_from_sv(r1234))
            << "rateValue " << rateValue;
}

/// G.5.1/G.4: create_signal_header() for Annex G's own worked example --
/// 16-QAM, R=3/4 (36 Mb/s), 100-octet PSDU -- must now reproduce Table G.12's
/// SIGNAL field waveform bit-for-bit, since the RATE codeword fix makes the
/// transmitted RATE bits match Table G.7 exactly (R1..R4 = 1,0,1,1).
TEST(Signal, CreateSignalHeaderMatchesAnnexGWaveform)
{
    const wifi80211a::LinkSettings link_settings(
        wifi80211a::ModulationTypes::QAM16, wifi80211a::CodingRates::R34);
    int psduLengthOctets = 100;

    const auto header = wifi80211a::create_signal_header(link_settings, psduLengthOctets);

    ASSERT_EQ(header.size(), kSignalOfdmSymbolGolden.size());
    for (std::size_t i = 0; i < header.size(); ++i)
        EXPECT_TRUE(wifi80211a::areClose(header[i], kSignalOfdmSymbolGolden[i], 1e-9)) << "sample " << i;
}

/// G.5.1: create_signal_header()/decode_signal_header() round trip for the
/// Annex G worked example itself -- a 100-octet PSDU sent at 36 Mb/s
/// (16-QAM, R=3/4).
TEST(Signal, CreateAndDecodeRoundTripAnnexGScenario)
{
    const wifi80211a::LinkSettings link_settings(
        wifi80211a::ModulationTypes::QAM16, wifi80211a::CodingRates::R34);
    int psduLengthOctets = 100;

    auto header = wifi80211a::create_signal_header(link_settings, psduLengthOctets);
    ASSERT_EQ(header.size(), 80u);

    wifi80211a::LinkSettings rx_link_settings;
    wifi80211a::complexVector empty_channel;
    const auto decoded = wifi80211a::decode_signal_header(rx_link_settings, header, empty_channel);

    EXPECT_EQ(decoded.modulationType, wifi80211a::ModulationTypes::QAM16);
    EXPECT_EQ(decoded.codingRate, wifi80211a::CodingRates::R34);
    EXPECT_EQ(decoded.psduLengthOctets, 100);
}

/// Round trip across every (modulation, coding-rate) pair the RATE field can
/// carry (17.3.4.1, Table 78) and both boundary and typical LENGTH values
/// (0, 1, the Annex G example's 100, and the max 12-bit value 4095).
TEST(Signal, RoundTripAllRateCombinations)
{
    for (const auto& [rateValue, modCoding] : wifi80211a::rateMap)
    {
        (void)rateValue;
        const auto [modulationType, codingRate] = modCoding;
        for (const int length : {0, 1, 100, 4095})
        {
            const wifi80211a::LinkSettings link_settings(modulationType, codingRate);
            int psduLengthOctets = length;
            auto header = wifi80211a::create_signal_header(link_settings, psduLengthOctets);

            wifi80211a::LinkSettings rx_link_settings;
            wifi80211a::complexVector empty_channel;
            const auto decoded = wifi80211a::decode_signal_header(rx_link_settings, header, empty_channel);

            EXPECT_EQ(decoded.modulationType, modulationType) << "length " << length;
            EXPECT_EQ(decoded.codingRate, codingRate) << "length " << length;
            EXPECT_EQ(decoded.psduLengthOctets, length);
        }
    }
}

/// 17.3.4: decode_signal_header() must reject a SIGNAL field whose parity bit
/// doesn't match the other 17 bits instead of silently accepting a corrupted
/// RATE/LENGTH. Built from Table G.7's bits with only the parity bit flipped
/// and pushed through the same encode/interleave/modulate primitives
/// create_signal_header() uses, so (noise-free) demodulation recovers every
/// bit -- including the deliberately wrong parity bit -- exactly.
TEST(Signal, DecodeRejectsParityMismatch)
{
    auto bits = bits_from_sv(kSignalFieldData);
    ASSERT_EQ(bits.size(), 18u);
    bits[17] ^= 1;

    const auto tailed = wifi80211a::append_convolutional_tail_(bits);
    const auto encoded = wifi80211a::convolutional_encoder_mother_(tailed);
    const auto interleaved = wifi80211a::interleave(encoded, 48, 1);
    const auto symbols = wifi80211a::map_bits_to_constellation(interleaved, wifi80211a::ModulationTypes::BPSK, 1);

    const wifi80211a::LinkSettings link_settings;
    auto header = wifi80211a::modulate(link_settings, symbols, wifi80211a::PilotLFSR());

    wifi80211a::LinkSettings rx_link_settings;
    wifi80211a::complexVector empty_channel;
    EXPECT_THROW(
        wifi80211a::decode_signal_header(rx_link_settings, header, empty_channel),
        std::runtime_error);
}
