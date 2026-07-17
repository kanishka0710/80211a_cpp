//
// Created by Kanishka on 4/7/2026.
//

#ifndef WIFI80211A_PREAMBLE_MODULE_H
#define WIFI80211A_PREAMBLE_MODULE_H

#include "phy/helpers.h"
#include "phy/link_settings.h"

namespace wifi80211a
{
    /// Subcarrier indices (k) carrying the Long Training Field, in transmission order.
    extern const std::vector<int> kLongTrainingSubcarriers;
    /// BPSK values (+1/-1) for each subcarrier in kLongTrainingSubcarriers.
    extern const std::vector<int> kLongTrainingValues;

    /// Generates the 10x-repeated 16-sample Short Training Field (160 samples total).
    complexVector generate_stf(const LinkSettings& linkSettings);

    /// Generates the Long Training Field: 32-sample cyclic prefix followed by two
    /// 64-sample symbols (160 samples total).
    complexVector generate_ltf(const LinkSettings& linkSettings);

    /// Generates the full preamble (STF followed by LTF).
    complexVector generate_preamble(const LinkSettings& linkSettings);
}

#endif //WIFI80211A_PREAMBLE_MODULE_H
