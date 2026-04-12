//
// Created by Kanishka on 4/8/2026.
//

#include "../include/phy/front_end_module.h"
#include <cstddef>
#include <fftw3.h>
#include <cmath>
#include <iostream>

#include "phy/link_settings.h"

namespace wifi80211a
{
    std::pmr::vector<std::complex<double>> FrontEndModule::pulse_shape(
        const std::pmr::vector<std::complex<double>>& signal, const double& T)
    {
        const int block_size = 256;
        std::vector<double> raised_cosine = generate_raised_cosine_(T);
        const int M = raised_cosine.size();
        const int nFFT = next_pow_of_2_(block_size + M - 1);

        // --- Buffers ---
        std::vector<fftw_complex> h_in(nFFT), H(nFFT); // filter
        std::vector<fftw_complex> x_in(nFFT), X(nFFT); // forward FFT of signal block
        std::vector<fftw_complex> y_in(nFFT), y_out(nFFT); // inverse FFT

        fftw_plan plan_h    = fftw_plan_dft_1d(nFFT, h_in.data(), H.data(),     FFTW_FORWARD,  FFTW_ESTIMATE);
        fftw_plan plan_fwd  = fftw_plan_dft_1d(nFFT, x_in.data(), X.data(),     FFTW_FORWARD,  FFTW_ESTIMATE);
        fftw_plan plan_inv  = fftw_plan_dft_1d(nFFT, y_in.data(), y_out.data(), FFTW_BACKWARD, FFTW_ESTIMATE);

        // Precompute H
        for (int i = 0; i < nFFT; i++)
        {
            h_in[i][0] = (i < M) ? raised_cosine[i] : 0.0;
            h_in[i][1] = 0.0;
        }
        fftw_execute(plan_h);

        // Upsample Signal
        std::vector<std::complex<double>> x_up(signal.size() * static_cast<std::size_t>(sps_), {0.0, 0.0});
        for (std::size_t i = 0; i < signal.size(); ++i)
            x_up[i * static_cast<std::size_t>(sps_)] = signal[i];

        std::vector<std::complex<double>> overlap(M - 1, {0.0, 0.0});
        std::pmr::vector<std::complex<double>> output_ofdm_waveform;

        const std::size_t x_up_len = x_up.size();
        const std::size_t block_size_u = static_cast<std::size_t>(block_size);
        for (std::size_t block_start = 0; block_start < x_up_len; block_start += block_size_u)
        {

            for (int i = 0; i < nFFT; i++) {
                const std::size_t idx = block_start + static_cast<std::size_t>(i);
                if (i < block_size && idx < x_up_len) {
                    x_in[i][0] = x_up[idx].real();
                    x_in[i][1] = x_up[idx].imag();
                } else {
                    x_in[i][0] = 0.0;
                    x_in[i][1] = 0.0;
                }
            }
            fftw_execute(plan_fwd);

            for (int k = 0; k < nFFT; k++)
            {
                y_in[k][0] = X[k][0]*H[k][0] - X[k][1]*H[k][1];
                y_in[k][1] = X[k][0]*H[k][1] + X[k][1]*H[k][0];
            }
            fftw_execute(plan_inv);

            for (int i = 0; i < nFFT; i++) {
                y_out[i][0] /= nFFT;
                y_out[i][1] /= nFFT;
            }

            for (int k = 0; k < M-1; k++)
            {
                y_out[k][0] += overlap[k].real();
                y_out[k][1] += overlap[k].imag();
            }

            for (int i = 0; i < M - 1; i++)
            {
                overlap[i] = { y_out[block_size + i][0], y_out[block_size + i][1] };
            }

            for (int i = 0; i < block_size; i++)
            {
                output_ofdm_waveform.emplace_back(y_out[i][0], y_out[i][1]);
            }
        }
        for (int i = 0; i < M - 1; i++)
        {
            output_ofdm_waveform.emplace_back(overlap[i]);
        }
        fftw_destroy_plan(plan_h);
        fftw_destroy_plan(plan_fwd);
        fftw_destroy_plan(plan_inv);
        return output_ofdm_waveform;
    }

    std::vector<double> FrontEndModule::iq_modulate(const std::pmr::vector<std::complex<double>> &signal, const double& T) const
    {
        double symbol_rate = 1.0 / T;
        double fs = symbol_rate * sps_;
        double fc = 5.8e9;
        std::vector<double> h(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++)
        {
            double t = (double) i / fs;
            h[i] = std::sqrt(2) * (signal[i].real() * std::cos(2*M_PI * fc * t) -
                signal[i].imag() * std::sin(2*M_PI * fc * t));
        }
        return h;
    }

    int FrontEndModule::next_pow_of_2_(unsigned int x)
    {
        // Implementation from: https://graphics.stanford.edu/%7Eseander/bithacks.html#RoundUpPowerOf2
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        x++;
        return x;
    }

    std::vector<double> FrontEndModule::generate_raised_cosine_(const double& T)
    {
        int filter_len = span_ * sps_ + 1;
        std::vector<double> h(filter_len);
        double C = 1 / std::sqrt(T * (1.0 - beta_ / 4));

        for (int i = 0; i < filter_len; i++)
        {
            double t = (double)(i - filter_len/2) / sps_;
            if (std::abs(t) < 1e-9)
            {
                h[i] = C;
            } else if (std::abs(t) - T / (2 * beta_) < 1e-9)
            {
                h[i] = C * M_PI / 4 * sinc_(1 / (2 * beta_));
            } else
            {
                h[i] = C * sinc_(t) * std::cos(M_PI * beta_ * t / T) / (1.0 - std::pow(2.0*beta_*t / T, 2));
            }
        }
        return h;
    }

    double FrontEndModule::sinc_(double x)
    {
        return std::sin(x * M_PI) / (x * M_PI);
    }
}
