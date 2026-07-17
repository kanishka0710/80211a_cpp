//
// Created by Kanishka on 4/7/2026.
//

#include "phy/preamble_module.h"

#include <cmath>
#include <complex>
#include <vector>
#include "phy/helpers.h"

namespace wifi80211a {
    
    // --- STF constants (Table 95, §17.3.3) ---
    const std::vector<int> kShortTrainingSubcarriers = {-24, -20, -16, -12, -8, -4, 4, 8, 12, 16, 20, 24};

    complexVector makeShortTrainingValues()
    {
        const std::complex<double> j(0.0, 1.0);
        complexVector values = {
            0.0, 0.0, 1.0 + j, 0.0, 0.0, 0.0, -1.0 - j, 0.0, 0.0, 0.0, 1.0 + j, 0.0,
            0.0, 0.0, -1.0 - j, 0.0, 0.0, 0.0, -1.0 - j, 0.0, 0.0, 0.0, 1.0 + j, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, -1.0 - j, 0.0, 0.0, 0.0, -1.0 - j, 0.0, 0.0, 0.0, 1.0 + j, 0.0, 0.0, 0.0, 1.0 + j,
            0.0, 0.0, 0.0, 1.0 + j, 0.0, 0.0, 0.0, 1.0 + j, 0.0, 0.0
        };
        const double scale = std::sqrt(13.0 / 6.0);
        for (auto& v : values) v *= scale;
        return values;
    }

    const complexVector kShortTrainingValues = makeShortTrainingValues();
    constexpr int kShortTrainingReps = 10; // 10 x 16-sample period = 160 samples

    // --- LTF constants (Table 95, §17.3.3) ---
    std::vector<int> makeLongTrainingSubcarriers()
    {
        std::vector<int> subcarriers;
        subcarriers.reserve(52);
        for (int k = -26; k < 0; ++k) subcarriers.push_back(k);
        for (int k = 1; k < 27; ++k) subcarriers.push_back(k);
        return subcarriers;
    }

    const std::vector<int> kLongTrainingSubcarriers = makeLongTrainingSubcarriers();

    const std::vector<int> kLongTrainingValues = {
        // k = -26 to k = -1
         1,  1, -1, -1,  1,  1, -1,  1, -1,  1,  1,  1,  1,  1,  1, -1,
        -1,  1,  1, -1,  1, -1,  1,  1,  1,  1,
        // k = 1 to k = 26 (k = 0 DC is skipped)
         1, -1, -1,  1,  1, -1,  1, -1,  1, -1, -1, -1, -1, -1,  1,  1,
        -1, -1,  1, -1,  1, -1,  1,  1,  1,  1,
    };

    constexpr int kLongTrainingCpLen = 32; // double-length guard interval

    complexVector generate_stf(const LinkSettings& linkSettings)
    {
        const int nFFT = linkSettings.getNFFT();
        complexVector freq_domain(nFFT, std::complex<double>(0.0, 0.0));

        for (const int k : kShortTrainingSubcarriers)
        {
            const int fftBin = fft_bin_from_subcarrier(k, nFFT);
            freq_domain[fftBin] = kShortTrainingValues[k + 26];
        }

        const complexVector time_domain = inverse_fft(freq_domain, nFFT);

        complexVector stf;
        stf.reserve(16 * kShortTrainingReps);
        for (int rep = 0; rep < kShortTrainingReps; ++rep)
        {
            stf.insert(stf.end(), time_domain.begin(), time_domain.begin() + 16);
        }
        return stf;
    }

    complexVector generate_ltf(const LinkSettings& linkSettings)
    {
        const int nFFT = linkSettings.getNFFT();
        complexVector freq_domain(nFFT, std::complex<double>(0.0, 0.0));

        for (std::size_t i = 0; i < kLongTrainingSubcarriers.size(); ++i)
        {
            const int fftBin = fft_bin_from_subcarrier(kLongTrainingSubcarriers[i], nFFT);
            freq_domain[fftBin] = std::complex<double>(static_cast<double>(kLongTrainingValues[i]), 0.0);
        }

        const complexVector time_domain = inverse_fft(freq_domain, nFFT);

        complexVector ltf;
        ltf.reserve(kLongTrainingCpLen + 2 * nFFT);
        ltf.insert(ltf.end(), time_domain.end() - kLongTrainingCpLen, time_domain.end()); // last 32 samples -> GI2
        ltf.insert(ltf.end(), time_domain.begin(), time_domain.end());
        ltf.insert(ltf.end(), time_domain.begin(), time_domain.end());
        return ltf;
    }

    complexVector generate_preamble(const LinkSettings& linkSettings)
    {
        complexVector preamble = generate_stf(linkSettings);
        const complexVector ltf = generate_ltf(linkSettings);
        preamble.insert(preamble.end(), ltf.begin(), ltf.end());
        return preamble;
    }
}
