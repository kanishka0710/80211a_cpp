
#include "phy/link_settings.h"
#include "phy/transceiver.h"
#include <matplot/matplot.h>

#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    const std::vector<int> input_bits = {
        1,1,1,0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,1,1,1,1,1,1,
    };

    wifi80211a::LinkSettings link_settings(
        wifi80211a::ModulationTypes::QPSK,
        wifi80211a::CodingRates::R12);

    auto pilotLfsr = wifi80211a::PilotLFSR();
    auto tx_signal = wifi80211a::Transceiver::generate_transmit_symbols(input_bits, link_settings, pilotLfsr);
    auto rx_bits = wifi80211a::Transceiver::receive_symbols(tx_signal, link_settings);
    rx_bits.resize(input_bits.size());

    for (std::size_t i = 0; i < input_bits.size(); ++i) {
        std::cout << "  [" << i << "]  RX = " << rx_bits[i] << ", TX = " << input_bits[i] << '\n';
    }


    return 0;
}
