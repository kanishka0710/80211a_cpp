#include "phy/preamble_module.h"

#include "phy/helpers.h"
#include "phy/link_settings.h"

#include <gtest/gtest.h>

#include <array>
#include <complex>

namespace {

// ── golden data from IEEE 802.11a-1999 Annex G ────────────────────────────────
//
// Annex G's short/long training tables (G.3, G.4, G.6) include a non-normative
// "windowing" step that tapers only the very first and very last sample of
// each field (multiplied by 0.5) to smooth the overlap-and-add join between
// consecutive fields. Since G.3.1 states this windowing "is not part of the
// normative specifications" and this codebase's generate_stf/generate_ltf do
// not implement it, the golden vectors below are the *unwindowed* IFFT
// outputs -- i.e. Annex G's own interior (non-edge) samples, which are
// windowed by exactly 1.0 and therefore equal the raw values our code
// produces. Every value was independently recomputed via IFFT of Tables G.2
// and G.5 and cross-checked against the corresponding interior entries of
// Tables G.4 and G.6.

/// One 16-sample period of the Short Training Field (raw IFFT of Table G.2).
/// generate_stf() repeats this block 10x to build the 160-sample STF.
constexpr std::array<std::complex<double>, 16> kStfPeriodGolden = {
    std::complex<double>{0.045998754512, 0.045998754512},
    std::complex<double>{-0.132443716852, 0.002339591885},
    std::complex<double>{-0.013472723270, -0.078524785754},
    std::complex<double>{0.142755292821, -0.012651167854},
    std::complex<double>{0.091997509024, -0.000000000000},
    std::complex<double>{0.142755292821, -0.012651167854},
    std::complex<double>{-0.013472723270, -0.078524785754},
    std::complex<double>{-0.132443716852, 0.002339591885},
    std::complex<double>{0.045998754512, 0.045998754512},
    std::complex<double>{0.002339591885, -0.132443716852},
    std::complex<double>{-0.078524785754, -0.013472723270},
    std::complex<double>{-0.012651167854, 0.142755292821},
    std::complex<double>{-0.000000000000, 0.091997509024},
    std::complex<double>{-0.012651167854, 0.142755292821},
    std::complex<double>{-0.078524785754, -0.013472723270},
    std::complex<double>{0.002339591885, -0.132443716852},
};

/// One 64-sample OFDM body of the Long Training Field (raw IFFT of Table G.5).
/// generate_ltf() emits [last-32-samples-of-body (CP)] + [body] + [body].
constexpr std::array<std::complex<double>, 64> kLtfBodyGolden = {
    std::complex<double>{0.156250000000, 0.000000000000},
    std::complex<double>{-0.005121250360, -0.120325132674},
    std::complex<double>{0.039749698354, -0.111157943051},
    std::complex<double>{0.096831884591, 0.082797909488},
    std::complex<double>{0.021111770349, 0.027885918828},
    std::complex<double>{0.059823844859, -0.087706759836},
    std::complex<double>{-0.115131214782, -0.055180495374},
    std::complex<double>{-0.038315967474, -0.106170912615},
    std::complex<double>{0.097541260736, -0.025888347648},
    std::complex<double>{0.053337734374, 0.004076326481},
    std::complex<double>{0.000988979709, -0.115004643624},
    std::complex<double>{-0.136804876816, -0.047379811366},
    std::complex<double>{0.024475851521, -0.058531795695},
    std::complex<double>{0.058668767129, -0.014938999451},
    std::complex<double>{-0.022483206308, 0.160657332953},
    std::complex<double>{0.119239088510, -0.004095594415},
    std::complex<double>{0.062500000000, -0.062500000000},
    std::complex<double>{0.036917942001, 0.098344150287},
    std::complex<double>{-0.057206345871, 0.039298588174},
    std::complex<double>{-0.131262608975, 0.065227229018},
    std::complex<double>{0.082218322303, 0.092356551954},
    std::complex<double>{0.069556847407, 0.014121958591},
    std::complex<double>{-0.060310100316, 0.081286124116},
    std::complex<double>{-0.056455128449, -0.021803920607},
    std::complex<double>{-0.035041260736, -0.150888347648},
    std::complex<double>{-0.121887009061, -0.016566218139},
    std::complex<double>{-0.127324359908, -0.020501379986},
    std::complex<double>{0.075073697068, -0.074040418925},
    std::complex<double>{-0.002805944173, 0.053774266477},
    std::complex<double>{-0.091887555263, 0.115128708911},
    std::complex<double>{0.091716549122, 0.105871659819},
    std::complex<double>{0.012284590459, 0.097599553592},
    std::complex<double>{-0.156250000000, 0.000000000000},
    std::complex<double>{0.012284590459, -0.097599553592},
    std::complex<double>{0.091716549122, -0.105871659819},
    std::complex<double>{-0.091887555263, -0.115128708911},
    std::complex<double>{-0.002805944173, -0.053774266477},
    std::complex<double>{0.075073697068, 0.074040418925},
    std::complex<double>{-0.127324359908, 0.020501379986},
    std::complex<double>{-0.121887009061, 0.016566218139},
    std::complex<double>{-0.035041260736, 0.150888347648},
    std::complex<double>{-0.056455128449, 0.021803920607},
    std::complex<double>{-0.060310100316, -0.081286124116},
    std::complex<double>{0.069556847407, -0.014121958591},
    std::complex<double>{0.082218322303, -0.092356551954},
    std::complex<double>{-0.131262608975, -0.065227229018},
    std::complex<double>{-0.057206345871, -0.039298588174},
    std::complex<double>{0.036917942001, -0.098344150287},
    std::complex<double>{0.062500000000, 0.062500000000},
    std::complex<double>{0.119239088510, 0.004095594415},
    std::complex<double>{-0.022483206308, -0.160657332953},
    std::complex<double>{0.058668767129, 0.014938999451},
    std::complex<double>{0.024475851521, 0.058531795695},
    std::complex<double>{-0.136804876816, 0.047379811366},
    std::complex<double>{0.000988979709, 0.115004643624},
    std::complex<double>{0.053337734374, -0.004076326481},
    std::complex<double>{0.097541260736, 0.025888347648},
    std::complex<double>{-0.038315967474, 0.106170912615},
    std::complex<double>{-0.115131214782, 0.055180495374},
    std::complex<double>{0.059823844859, 0.087706759836},
    std::complex<double>{0.021111770349, -0.027885918828},
    std::complex<double>{0.096831884591, -0.082797909488},
    std::complex<double>{0.039749698354, 0.111157943051},
    std::complex<double>{-0.005121250360, 0.120325132674},
};

} // namespace

// ── individual tests ──────────────────────────────────────────────────────────

/// G.3/G.4: generate_stf() must be the 16-sample raw IFFT period, repeated 10x.
TEST(Preamble, ShortTrainingFieldMatchesAnnexG)
{
    const wifi80211a::LinkSettings link_settings;
    const auto stf = wifi80211a::generate_stf(link_settings);
    ASSERT_EQ(stf.size(), 160u);
    for (std::size_t i = 0; i < stf.size(); ++i)
    {
        EXPECT_TRUE(wifi80211a::areClose(stf[i], kStfPeriodGolden[i % 16], 1e-6))
            << "mismatch at sample " << i;
    }
}

/// G.5/G.6: generate_ltf() must be [last 32 samples of the raw IFFT body (CP)]
/// followed by two copies of the 64-sample raw IFFT body.
TEST(Preamble, LongTrainingFieldMatchesAnnexG)
{
    const wifi80211a::LinkSettings link_settings;
    const auto ltf = wifi80211a::generate_ltf(link_settings);
    ASSERT_EQ(ltf.size(), 160u);

    for (int i = 0; i < 32; ++i)
        EXPECT_TRUE(wifi80211a::areClose(ltf[i], kLtfBodyGolden[32 + i], 1e-6)) << "CP sample " << i;
    for (int i = 0; i < 64; ++i)
        EXPECT_TRUE(wifi80211a::areClose(ltf[32 + i], kLtfBodyGolden[i], 1e-6)) << "body #1 sample " << i;
    for (int i = 0; i < 64; ++i)
        EXPECT_TRUE(wifi80211a::areClose(ltf[96 + i], kLtfBodyGolden[i], 1e-6)) << "body #2 sample " << i;
}

/// generate_preamble() must simply be the STF immediately followed by the LTF.
TEST(Preamble, FullPreambleIsStfThenLtf)
{
    const wifi80211a::LinkSettings link_settings;
    const auto stf = wifi80211a::generate_stf(link_settings);
    const auto ltf = wifi80211a::generate_ltf(link_settings);
    const auto preamble = wifi80211a::generate_preamble(link_settings);

    ASSERT_EQ(preamble.size(), stf.size() + ltf.size());
    for (std::size_t i = 0; i < stf.size(); ++i)
        EXPECT_TRUE(wifi80211a::areClose(preamble[i], stf[i]));
    for (std::size_t i = 0; i < ltf.size(); ++i)
        EXPECT_TRUE(wifi80211a::areClose(preamble[stf.size() + i], ltf[i]));
}
