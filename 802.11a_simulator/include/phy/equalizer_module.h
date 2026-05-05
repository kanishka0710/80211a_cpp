//
// Created by Kanishka on 4/19/2026.
//

#ifndef WIFI80211A_EQUALIZER_MODULE_H
#define WIFI80211A_EQUALIZER_MODULE_H
#include "ofdm_module.h"

namespace wifi80211a
{
    class EqualizerModule {
    public:
        EqualizerModule() = default;
        static complexVector perform_equalization(
            OFDMDemodResult& result, 
            const LinkSettings& link_settings);
    };
}



#endif //WIFI80211A_EQUALIZER_MODULE_H
