#pragma once
#include <complex>
#include <memory_resource>
#include <vector>

namespace wifi80211a
{
    int fft_bin_from_subcarrier(const int& k, const int& nFFT);

    bool areClose(const std::complex<double>& a, const std::complex<double>& b, double epsilon = 1e-9);

    using complexVector = std::vector<std::complex<double>>;

    complexVector fft(const complexVector& time_domain, int nFFT);
    complexVector inverse_fft(const complexVector& freq_domain, int nFFT);

    std::vector<int> int_to_bits(const uint64_t n, int numBits = -1);
}
