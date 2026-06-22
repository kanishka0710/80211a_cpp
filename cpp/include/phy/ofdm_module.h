#pragma once
#include <vector>
#include <complex>
#include <cstdint>
#include "phy/helpers.h"
#include "phy/link_settings.h"
#include "phy/pilot_utils.h"

namespace wifi80211a {

    /// Output of `OFDMModulator::demodulate`: full FFT bins per symbol plus pilot taps.
    struct OFDMDemodResult {
        /// Concatenated per-symbol FFT outputs, bin order [0 .. nFFT-1] repeated for each symbol.
        complexVector freq_bins;

        /// Received pilot subcarriers in `LinkSettings::getPilotPositions()` order
        /// (−21, −7, +7, +21), four complexes per OFDM symbol.
        complexVector pilots;

        /// Expected (reference) pilot values in the same order as `pilots`, computed from
        /// the pilot LFSR and per-subcarrier polarity weights during demodulation.
        /// Dividing `pilots[i]` by `reference_pilots[i]` gives the channel estimate at
        /// that pilot subcarrier.
        complexVector reference_pilots;
    };

    class OFDMModule {
    public:
        OFDMModule() = default;
        static complexVector modulate(const LinkSettings& link_settings,
            const complexVector& data, PilotLFSR pilotLfsr);
        static OFDMDemodResult demodulate(const LinkSettings& link_settings,
            const complexVector& data);
    };
}
