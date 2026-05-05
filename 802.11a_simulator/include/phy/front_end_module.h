//
// Created by Kanishka on 4/8/2026.
//

#ifndef WIFI80211A_FRONT_END_MODULE_H
#define WIFI80211A_FRONT_END_MODULE_H
#include <complex>
#include <vector>

#include "link_settings.h"
#include "phy/helpers.h"

namespace wifi80211a
{

    class FrontEndModule {
    public:
        FrontEndModule() = default;

        // ── TX chain ─────────────────────────────────────────────────────────

        /// Upsamples by sps_ then applies the RRC pulse-shaping filter.
        complexVector pulse_shape(const complexVector& signal, const double& T);

        /// Mixes complex baseband up to a real passband signal at fc = 5.8 GHz.
        std::vector<double> iq_modulate(const complexVector& signal, const double& T) const;

        // ── RX chain ─────────────────────────────────────────────────────────

        /// Downconverts a real passband signal back to complex baseband.
        /// Input must be std::vector<double> — the direct output of iq_modulate.
        complexVector iq_demodulate(const std::vector<double>& signal, const double& T) const;

        /// Applies the RRC matched filter (completing the Nyquist RC response
        /// end-to-end) then downsamples by sps_ to recover 1× rate OFDM symbols.
        /// Combined TX + RX group delay ((M-1) samples at 4× rate) is compensated
        /// automatically so the output aligns with OFDM symbol boundaries.
        complexVector matched_filter(const complexVector& signal);

    private:
        /// Generates the Root Raised Cosine impulse response (length span_*sps_+1).
        /// Uses normalised time (T = 1 symbol period) so no T argument is needed.
        std::vector<double> generate_rrc_() const;

        static double sinc_(double x);
        static int next_pow_of_2_(unsigned int x);

        const int    sps_  = 4;    // samples per symbol (upsampling factor)
        const double beta_ = 0.25; // RRC roll-off factor
        const int    span_ = 5;    // filter spans ±span_ symbol periods
    };
}



#endif //WIFI80211A_FRONT_END_MODULE_H
