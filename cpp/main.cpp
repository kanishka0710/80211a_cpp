#include "phy/link_settings.h"
#include "phy/rx_chain.h"
#include "phy/tx_chain.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace wifi80211a;

namespace
{
    std::vector<int> string_to_bits(const std::string& message)
    {
        std::vector<int> bits;
        bits.reserve(message.size() * 8);
        for (const unsigned char c : message)
        {
            for (int i = 7; i >= 0; --i)
            {
                bits.push_back((c >> i) & 1);
            }
        }
        return bits;
    }

    std::string bits_to_string(const std::vector<int>& bits, const std::size_t num_bytes)
    {
        std::string message;
        message.reserve(num_bytes);
        for (std::size_t byte_i = 0; byte_i < num_bytes; ++byte_i)
        {
            unsigned char c = 0;
            for (int i = 0; i < 8; ++i)
            {
                c = static_cast<unsigned char>((c << 1) | (bits[byte_i * 8 + i] & 1));
            }
            message.push_back(static_cast<char>(c));
        }
        return message;
    }
}

int main()
{
    const std::string message =
        "Joy, bright spark of divinity,\n"
        "Daughter of Elysium,\n"
        "Fire-insired we tread\n"
        "Thy sanctuary.\n"
        "Thy magic power re-unites\n"
        "All that custom has divided,\n"
        "All men become brothers\n"
        "Under the sway of thy gentle wings...\n";

    const std::vector<int> bits = string_to_bits(message);
    const int num_bits = static_cast<int>(bits.size());

    const LinkSettings link_settings(ModulationTypes::BPSK, CodingRates::R12);
    constexpr std::uint8_t scrambler_seed = 0x5D;

    const complexVector tx_signal = generate_tx_signal(bits, link_settings, scrambler_seed);
    const complexVector& rx_signal = tx_signal; // ideal loopback (no channel impairments)
    const RxResult rx = receive(rx_signal, link_settings, scrambler_seed);

    const int n_compare = std::min(num_bits, static_cast<int>(rx.bits.size()));
    int bit_errors = 0;
    for (int i = 0; i < n_compare; ++i)
    {
        if (bits[i] != rx.bits[i]) ++bit_errors;
    }
    const double ber = n_compare > 0 ? static_cast<double>(bit_errors) / n_compare : 0.0;

    const std::string received = bits_to_string(rx.bits, message.size());

    std::cout << "Original message:\n" << message << "\n\n";
    std::cout << "Received message:\n" << received << "\n\n";
    std::cout << "Bits compared: " << n_compare
              << "  errors: " << bit_errors
              << "  BER: " << ber << '\n';
    std::cout << "Match: " << (message == received ? "yes" : "no") << '\n';

    return 0;
}
