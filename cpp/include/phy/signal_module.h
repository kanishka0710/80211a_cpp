//
// Created by Kanishka Roy on 7/23/26.
//

#ifndef WIFI80211A_SIGNAL_MODULE_H
#define WIFI80211A_SIGNAL_MODULE_H
#include "helpers.h"
#include "link_settings.h"


namespace wifi80211a
{
    struct SignalOutput
    {
        ModulationTypes modulationType;
        CodingRates codingRate;
        int psduLengthOctets;
    };

    complexVector create_signal_header(const LinkSettings& linkSettings, int& psduLengthOctets);
    SignalOutput decode_signal_header(LinkSettings& linkSettings, complexVector& signalHeader, complexVector& H);
}


#endif //WIFI80211A_SIGNAL_MODULE_H
