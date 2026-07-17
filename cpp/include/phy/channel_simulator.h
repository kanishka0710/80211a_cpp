//
// Created by kanishka on 7/15/26.
//

#ifndef WIFI80211A_CHANNEL_SIMULATOR_H
#define WIFI80211A_CHANNEL_SIMULATOR_H

#include <random>

#include "phy/helpers.h"

namespace wifi80211a
{
    /// Prepends `delaySamples` of unit-power complex Gaussian noise to `signal`.
    complexVector add_delay(const complexVector& signal, int delaySamples, std::mt19937& gen);

    /// Adds complex AWGN scaled to the given per-sample SNR (in dB).
    complexVector add_awgn(const complexVector& signal, double snr_db, std::mt19937& gen);

    complexVector add_multipath(const complexVector& signal);
}


#endif //WIFI80211A_CHANNEL_SIMULATOR_H
