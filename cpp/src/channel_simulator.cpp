//
// Created by kanishka on 7/15/26.
//

#include "../include/phy/channel_simulator.h"

#include <cmath>

namespace wifi80211a
{
    complexVector add_delay(const complexVector& signal, int delaySamples, std::mt19937& gen)
    {
        std::normal_distribution<double> normDistr(0, 1);
        constexpr double inv_sqrt2 = 0.7071067811865476; // 1/sqrt(2), unit-power scaling

        complexVector delayedSignal;
        delayedSignal.reserve(static_cast<std::size_t>(delaySamples) + signal.size());
        for (int i = 0; i < delaySamples; i++)
        {
            delayedSignal.emplace_back(normDistr(gen) * inv_sqrt2, normDistr(gen) * inv_sqrt2);
        }
        delayedSignal.insert(delayedSignal.end(), signal.begin(), signal.end());
        return delayedSignal;
    }

    complexVector add_awgn(const complexVector& signal, double snr_db, std::mt19937& gen)
    {
        double signalPower = 0.0;
        for (const auto& sample : signal)
        {
            signalPower += std::norm(sample);
        }
        signalPower /= static_cast<double>(signal.size());

        const double noisePower = signalPower / std::pow(10.0, snr_db / 10.0);
        const double sigma = std::sqrt(noisePower / 2.0);

        std::normal_distribution<double> normDistr(0, 1);
        complexVector noisySignal(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++)
        {
            noisySignal[i] = signal[i] + std::complex<double>(normDistr(gen) * sigma, normDistr(gen) * sigma);
        }
        return noisySignal;
    }

    complexVector add_multipath(const complexVector& signal)
    {
        return signal;
    }
}
