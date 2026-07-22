//
// C++ port of python/loopback_test.py
//
// 802.11a TX -> AWGN channel -> RX loopback test.
//
// Runs BER sweeps across SNR values for every modulation / coding-rate
// combination, then prints a summary table and saves a BER-vs-SNR plot.
//

#include "phy/channel_simulator.h"
#include "phy/link_settings.h"
#include "phy/rx_chain.h"
#include "phy/tx_chain.h"

#include <matplot/matplot.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace wifi80211a;

namespace
{
    // ── naming helpers ──────────────────────────────────────────────────────

    std::string modulation_name(const ModulationTypes modulation)
    {
        switch (modulation)
        {
        case ModulationTypes::BPSK: return "BPSK";
        case ModulationTypes::QPSK: return "QPSK";
        case ModulationTypes::QAM16: return "QAM16";
        case ModulationTypes::QAM64: return "QAM64";
        }
        return "?";
    }

    std::string coding_rate_name(const CodingRates coding_rate)
    {
        switch (coding_rate)
        {
        case CodingRates::R12: return "R12";
        case CodingRates::R23: return "R23";
        case CodingRates::R34: return "R34";
        }
        return "?";
    }

    // ── single trial ────────────────────────────────────────────────────────

    struct TrialResult
    {
        double ber;
        int bit_errors;
        int bits_compared;
        int packet_start;
        double cfo_hz;
        int true_delay;
    };

    /// One TX -> channel -> RX trial.
    TrialResult run_trial(
        const int num_bits,
        const LinkSettings& link_settings,
        const double snr_db,
        const std::uint8_t scrambler_seed,
        const unsigned seed)
    {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<int> bit_dist(0, 1);

        std::vector<int> bits(num_bits);
        for (int& b : bits) b = bit_dist(gen);

        const complexVector tx_signal = generate_tx_signal(bits, link_settings, scrambler_seed);

        // Per-SNR delay offset keeps trials reproducible while still stressing sync.
        std::uniform_int_distribution<int> delay_dist(0, 234);
        const complexVector noisy_signal = add_awgn(tx_signal, snr_db, gen);
        const int delay = delay_dist(gen);
        complexVector rx_signal = add_delay(noisy_signal, delay, gen);
        std::uniform_real_distribution cfo_dist(-100e3, 100e3);
        const double cfo_hz = cfo_dist(gen);
        std::uniform_real_distribution<double> phase_dist(-M_PI, M_PI);
        const double phaseDelay = phase_dist(gen);
        rx_signal = add_cfo_and_phase(rx_signal, cfo_hz, 20e6, phaseDelay);

        const RxResult rx = receive(rx_signal, link_settings, scrambler_seed);

        // Compare only the original num_bits (RX may produce tail-padding).
        const int n_compare = std::min<int>(num_bits, static_cast<int>(rx.bits.size()));
        int bit_errors = 0;
        for (int i = 0; i < n_compare; i++)
        {
            if (bits[i] != rx.bits[i]) bit_errors++;
        }
        const double ber = n_compare > 0 ? static_cast<double>(bit_errors) / n_compare : 0.0;

        return TrialResult{ber, bit_errors, n_compare, rx.sync.packet_start, rx.sync.cfo_hz, delay};
    }

    // ── SNR sweep ────────────────────────────────────────────────────────────

    std::vector<double> snr_sweep(
        const int num_bits,
        const LinkSettings& link_settings,
        const std::vector<double>& snr_range,
        const unsigned seed = 42)
    {
        std::vector<double> bers;
        bers.reserve(snr_range.size());
        for (const double snr_db : snr_range)
        {
            const TrialResult result = run_trial(num_bits, link_settings, snr_db, 0x5D, seed);
            bers.push_back(result.ber);
            std::cout
                << "  " << std::left << std::setw(5) << modulation_name(link_settings.getModulationType())
                << " " << std::setw(3) << coding_rate_name(link_settings.getCodingRate())
                << "  SNR=" << std::right << std::setw(5) << std::fixed << std::setprecision(1) << snr_db << " dB"
                << "  BER=" << std::setprecision(4) << result.ber
                << "  errors=" << result.bit_errors << "/" << result.bits_compared
                << "  pkt_start=" << result.packet_start
                << "  cfo=" << std::setprecision(1) << result.cfo_hz << " Hz"
                << "  true_delay=" << result.true_delay << " samples"
                << '\n';
        }
        return bers;
    }
} // namespace

// ── main ──────────────────────────────────────────────────────────────────

int main()
{
    constexpr int NUM_BITS = 1e4;
    const std::vector<double> SNR_RANGE = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};

    const std::vector<std::pair<ModulationTypes, CodingRates>> CONFIGS = {
        {ModulationTypes::BPSK, CodingRates::R12},
        {ModulationTypes::BPSK, CodingRates::R23},
        {ModulationTypes::BPSK, CodingRates::R34},
        {ModulationTypes::QPSK, CodingRates::R12},
        {ModulationTypes::QPSK, CodingRates::R23},
        {ModulationTypes::QPSK, CodingRates::R34},
        {ModulationTypes::QAM16, CodingRates::R12},
        {ModulationTypes::QAM16, CodingRates::R23},
        {ModulationTypes::QAM16, CodingRates::R34},
        {ModulationTypes::QAM64, CodingRates::R12},
        {ModulationTypes::QAM64, CodingRates::R23},
        {ModulationTypes::QAM64, CodingRates::R34},
    };

    matplot::hold(matplot::on);
    std::vector<std::string> labels;

    for (const auto& [modulation, coding_rate] : CONFIGS)
    {
        const LinkSettings link_settings(modulation, coding_rate);
        const std::string label = modulation_name(modulation) + " " + coding_rate_name(coding_rate);
        labels.push_back(label);

        std::cout << "\n-- " << label << " --\n";
        const std::vector<double> bers = snr_sweep(NUM_BITS, link_settings, SNR_RANGE);

        std::vector<double> clamped_bers;
        clamped_bers.reserve(bers.size());
        for (const double b : bers) clamped_bers.push_back(std::max(b, 1e-5));

        auto p = matplot::semilogy(SNR_RANGE, clamped_bers, "-o");
        p->display_name(label);
    }

    matplot::xlabel("SNR (dB)");
    matplot::ylabel("BER");
    matplot::title("802.11a Loopback -- BER vs SNR  (AWGN)");
    matplot::legend(labels);
    matplot::grid(matplot::on);
    matplot::ylim({1e-5, 1.0});

    matplot::save("ber_vs_snr.png");
    matplot::show();
    std::cout << "\nSaved -> ber_vs_snr.png\n";

    return 0;
}
