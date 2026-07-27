//
// Created by Kanishka Roy on 7/23/26.
//

#include "../include/phy/signal_module.h"

#include <unistd.h>

namespace wifi80211a
{
    complexVector create_signal_header(const LinkSettings& linkSettings, int& psduLengthOctets) {
        std::vector<int> signalBits;
        signalBits.insert(signalBits.end(), int_to_bits(inverseRateMap[u07
            {linkSettings.getModulationType(), linkSettings.getCodingRate()}], 4));
        signalBits.emplace_back(0);


    }

    SignalOutput decode_signal_header(LinkSettings& linkSettings, int& psduLengthOctets)
    {

    }
}
