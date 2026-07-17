//
// Created by Kanishka on 5/4/2026.
//
#include "phy/helpers.h"

#include <fftw3.h>

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

    complexVector inverse_fft(const complexVector& freq_domain, const int nFFT)
    {
        std::vector<fftw_complex> in(nFFT), out(nFFT);
        for (int i = 0; i < nFFT; ++i)
        {
            in[i][0] = freq_domain[i].real();
            in[i][1] = freq_domain[i].imag();
        }

        fftw_plan plan = fftw_plan_dft_1d(nFFT, in.data(), out.data(), FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(plan);
        fftw_destroy_plan(plan);

        complexVector time_domain(nFFT);
        for (int i = 0; i < nFFT; ++i)
        {
            time_domain[i] = std::complex<double>(out[i][0], out[i][1]) / static_cast<double>(nFFT);
        }
        return time_domain;
    }

    complexVector fft(const complexVector& time_domain, const int nFFT)
    {
        std::vector<fftw_complex> in(nFFT), out(nFFT);
        for (int i = 0; i < nFFT; ++i)
        {
            in[i][0] = time_domain[i].real();
            in[i][1] = time_domain[i].imag();
        }

        fftw_plan plan = fftw_plan_dft_1d(nFFT, in.data(), out.data(), FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);
        fftw_destroy_plan(plan);

        complexVector freq_domain(nFFT);
        for (int i = 0; i < nFFT; ++i)
        {
            freq_domain[i] = std::complex<double>(out[i][0], out[i][1]);
        }
        return freq_domain;
    }

}
