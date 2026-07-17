//
// Created by Kanishka on 4/19/2026.
//

#ifndef WIFI80211A_EQUALIZER_MODULE_H
#define WIFI80211A_EQUALIZER_MODULE_H
#include "ofdm_module.h"

namespace wifi80211a
{
    complexVector perform_equalization(OFDMDemodResult& result, const LinkSettings& link_settings);

    /// Per-subcarrier equalization using an LTF-derived channel estimate `H`
    /// (as produced by `channel_estimation`). `freqBins` is the flattened
    /// FFT output from `demodulate`, shape = (num_symbols * nFFT).
    complexVector equalize_with_ltf(const complexVector& freqBins, const complexVector& H, const LinkSettings& linkSettings);
}

#endif //WIFI80211A_EQUALIZER_MODULE_H
