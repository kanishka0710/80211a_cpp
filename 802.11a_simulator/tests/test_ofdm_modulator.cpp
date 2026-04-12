#include "phy/link_settings.h"
#include "phy/ofdm_modulator.h"
#include <numbers>
#include <vector>
#include <complex>
#include <cmath>
#include <memory_resource>
#include <matplot/matplot.h>

namespace wifi80211a {

int test_ofdm_modulator() {
    wifi80211a::LinkSettings link_settings = wifi80211a::LinkSettings();
    wifi80211a::OFDMModulator modulator = wifi80211a::OFDMModulator();

    const double fs = 20e6;
    const double f = 100e3;
    const std::size_t num_samples = 1024;

    std::pmr::vector<std::complex<double>> test_data;
    std::vector<double> frequency;
    for (std::size_t i = 0; i < num_samples; i++) {
        double phase = 2.0 * std::numbers::pi * f * static_cast<double>(i) / fs;
        test_data.emplace_back(std::cos(phase), std::sin(phase));
        frequency.push_back(f * static_cast<double>(i) / fs);
    }

    auto out = modulator.modulate(link_settings, test_data);

    std::vector<double> magnitude;
    magnitude.reserve(out.size());
    for (const auto& sample : out) {
        magnitude.push_back(std::abs(sample));
    }
    std::vector<double> sample_idx(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        sample_idx[i] = static_cast<double>(i);
    }
    matplot::plot(sample_idx, magnitude);
    matplot::show();
    return 0;
}

}