//
// Created by Kanishka on 5/4/2026.
//

#ifndef WIFI80211A_TRANSMITTER_H
#define WIFI80211A_TRANSMITTER_H
#include <vector>

#include "equalizer_module.h"
#include "fec_module.h"
#include "helpers.h"
#include "interleaver_module.h"
#include "modulation_module.h"
#include "ofdm_module.h"
#include "phy/link_settings.h"

namespace wifi80211a {

    complexVector generate_transmit_symbols(const std::vector<int>& bits, LinkSettings& linkSettings, PilotLFSR& pilotLfsr);
    std::vector<int> receive_symbols(complexVector& signal, LinkSettings& linkSettings);

} // wifi80211a

#endif //WIFI80211A_TRANSMITTER_H
