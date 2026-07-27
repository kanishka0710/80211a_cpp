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

    // fftw_complex is a raw double[2] array type, which recent libc++ versions
    // reject as a std::vector element type. C++11 guarantees std::complex<double>
    // has the same layout as double[2], so we operate on complexVector directly
    // and reinterpret_cast when handing buffers to FFTW.
    complexVector inverse_fft(const complexVector& freq_domain, const int nFFT)
    {
        complexVector in(freq_domain.begin(), freq_domain.begin() + nFFT);
        complexVector out(nFFT);

        fftw_plan plan = fftw_plan_dft_1d(nFFT,
            reinterpret_cast<fftw_complex*>(in.data()),
            reinterpret_cast<fftw_complex*>(out.data()),
            FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(plan);
        fftw_destroy_plan(plan);

        for (auto& sample : out)
        {
            sample /= static_cast<double>(nFFT);
        }
        return out;
    }

    complexVector fft(const complexVector& time_domain, const int nFFT)
    {
        complexVector in(time_domain.begin(), time_domain.begin() + nFFT);
        complexVector out(nFFT);

        fftw_plan plan = fftw_plan_dft_1d(nFFT,
            reinterpret_cast<fftw_complex*>(in.data()),
            reinterpret_cast<fftw_complex*>(out.data()),
            FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);
        fftw_destroy_plan(plan);

        return out;
    }


    // LSB-first bit vector (bit 0 = LSB), matching 802.11a transmit order.
    // If numBits < 0 (or omitted), compute the minimal number of bits needed for n.
    std::vector<int> int_to_bits(const uint64_t n, int numBits) {
        if (numBits < 0) {
            // Minimal bit-width: number of bits needed to represent n (n=0 -> 1 bit, matching Python's bin(0) = "0b0")
            int width = 1;
            uint64_t temp = n;
            while (temp >>= 1) {
                width++;
            }
            numBits = width;
        }

        std::vector<int> bits(numBits);
        for (int i = 0; i < numBits; ++i) {
            bits[i] = (n >> i) & 1;
        }
        return bits;
    }

}
