//
// Created by Kanishka on 5/4/2026.
//
#include "phy/helpers.h"

namespace wifi80211a
{
    int fft_bin_from_subcarrier(const int& k, const int& nFFT)
    {
        return (k > 0) ? k : nFFT + k;
    }

    bool areClose(const std::complex<double>& a, const std::complex<double>& b, const double epsilon)
    {
        return std::abs(a - b) < epsilon;
    }
}
