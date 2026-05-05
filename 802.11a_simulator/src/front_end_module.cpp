//
// Created by Kanishka on 4/8/2026.
//

#include "../include/phy/front_end_module.h"
#include <cstddef>
#include <fftw3.h>
#include <cmath>

#include "phy/helpers.h"
#include "phy/link_settings.h"

namespace wifi80211a {

    static int next_pow_of_2_impl(unsigned int x)
    {
        // https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        return static_cast<int>(x + 1);
    }

    /// Full linear convolution of a complex signal with a real filter via
    /// frequency-domain overlap-add.  Output length = signal.size() + M - 1.
    complexVector overlap_add_convolve(
        const std::vector<std::complex<double>>& signal,
        const std::vector<double>&               filter)
    {
        const int block_size = 256;
        const int M    = static_cast<int>(filter.size());
        const int nFFT = next_pow_of_2_impl(static_cast<unsigned int>(block_size + M - 1));

        std::vector<fftw_complex> h_in(nFFT), H(nFFT);
        std::vector<fftw_complex> x_in(nFFT), X(nFFT);
        std::vector<fftw_complex> y_in(nFFT), y_out(nFFT);

        fftw_plan plan_h   = fftw_plan_dft_1d(nFFT, h_in.data(), H.data(),     FFTW_FORWARD,  FFTW_ESTIMATE);
        fftw_plan plan_fwd = fftw_plan_dft_1d(nFFT, x_in.data(), X.data(),     FFTW_FORWARD,  FFTW_ESTIMATE);
        fftw_plan plan_inv = fftw_plan_dft_1d(nFFT, y_in.data(), y_out.data(), FFTW_BACKWARD, FFTW_ESTIMATE);

        for (int i = 0; i < nFFT; i++) {
            h_in[i][0] = (i < M) ? filter[i] : 0.0;
            h_in[i][1] = 0.0;
        }
        fftw_execute(plan_h);

        std::vector<std::complex<double>> overlap(M - 1, {0.0, 0.0});
        complexVector output;

        const std::size_t sig_len      = signal.size();
        const std::size_t block_size_u = static_cast<std::size_t>(block_size);

        for (std::size_t block_start = 0; block_start < sig_len; block_start += block_size_u)
        {
            for (int i = 0; i < nFFT; i++) {
                const std::size_t idx = block_start + static_cast<std::size_t>(i);
                if (i < block_size && idx < sig_len) {
                    x_in[i][0] = signal[idx].real();
                    x_in[i][1] = signal[idx].imag();
                } else {
                    x_in[i][0] = 0.0;
                    x_in[i][1] = 0.0;
                }
            }
            fftw_execute(plan_fwd);

            for (int k = 0; k < nFFT; k++) {
                y_in[k][0] = X[k][0]*H[k][0] - X[k][1]*H[k][1];
                y_in[k][1] = X[k][0]*H[k][1] + X[k][1]*H[k][0];
            }
            fftw_execute(plan_inv);

            for (int i = 0; i < nFFT; i++) {
                y_out[i][0] /= nFFT;
                y_out[i][1] /= nFFT;
            }

            for (int k = 0; k < M - 1; k++) {
                y_out[k][0] += overlap[k].real();
                y_out[k][1] += overlap[k].imag();
            }

            for (int i = 0; i < M - 1; i++)
                overlap[i] = {y_out[block_size + i][0], y_out[block_size + i][1]};

            for (int i = 0; i < block_size; i++)
                output.emplace_back(y_out[i][0], y_out[i][1]);
        }

        for (int i = 0; i < M - 1; i++)
            output.emplace_back(overlap[i]);

        fftw_destroy_plan(plan_h);
        fftw_destroy_plan(plan_fwd);
        fftw_destroy_plan(plan_inv);

        return output;
    }

    // Class-method wrappers delegating to the file-local helpers
    int FrontEndModule::next_pow_of_2_(unsigned int x) { return next_pow_of_2_impl(x); }
    double FrontEndModule::sinc_(double x) { return std::sin(x * M_PI) / (x * M_PI); }

    /// Root Raised Cosine impulse response.
    /// Time is normalised so one symbol period = 1.0 (i.e. t = n/sps_).
    /// TX and RX both use this same filter; together they form a full Nyquist RC.
    std::vector<double> FrontEndModule::generate_rrc_() const
    {
        const int filter_len = span_ * sps_ + 1;
        std::vector<double> h(filter_len);

        for (int i = 0; i < filter_len; i++)
        {
            const double t = static_cast<double>(i - filter_len / 2) / sps_; // symbol periods

            if (std::abs(t) < 1e-9)
            {
                // Special case: t = 0
                h[i] = 1.0 - beta_ + 4.0 * beta_ / M_PI;
            }
            else if (std::abs(std::abs(t) - 1.0 / (4.0 * beta_)) < 1e-9)
            {
                // Special case: t = ±1/(4β)  — denominator of general formula hits zero
                h[i] = (beta_ / std::sqrt(2.0)) *
                       ((1.0 + 2.0 / M_PI) * std::sin(M_PI / (4.0 * beta_)) +
                        (1.0 - 2.0 / M_PI) * std::cos(M_PI / (4.0 * beta_)));
            }
            else
            {
                // General case
                h[i] = (std::sin(M_PI * t * (1.0 - beta_)) +
                        4.0 * beta_ * t * std::cos(M_PI * t * (1.0 + beta_))) /
                       (M_PI * t * (1.0 - std::pow(4.0 * beta_ * t, 2.0)));
            }
        }
        return h;
    }

    // ── TX chain ─────────────────────────────────────────────────────────────

    complexVector FrontEndModule::pulse_shape(
        const complexVector& signal, const double& /*T*/)
    {
        const std::vector<double> rrc = generate_rrc_();

        // Upsample: insert (sps_-1) zeros between each complex sample
        std::vector<std::complex<double>> upsampled(
            signal.size() * static_cast<std::size_t>(sps_), {0.0, 0.0});
        for (std::size_t i = 0; i < signal.size(); ++i)
            upsampled[i * static_cast<std::size_t>(sps_)] = signal[i];

        return overlap_add_convolve(upsampled, rrc);
    }

    std::vector<double> FrontEndModule::iq_modulate(
        const complexVector& signal, const double& T) const
    {
        const double fs = (1.0 / T) * sps_;
        const double fc = 5.8e9;
        std::vector<double> passband(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++) {
            const double t = static_cast<double>(i) / fs;
            passband[i] = std::sqrt(2.0) * (signal[i].real() * std::cos(2.0 * M_PI * fc * t) -
                                             signal[i].imag() * std::sin(2.0 * M_PI * fc * t));
        }
        return passband;
    }

    // ── RX chain ─────────────────────────────────────────────────────────────

    complexVector FrontEndModule::iq_demodulate(
        const std::vector<double>& signal, const double& T) const
    {
        const double fs = (1.0 / T) * sps_;
        const double fc = 5.8e9;
        complexVector baseband(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++) {
            const double t = static_cast<double>(i) / fs;
            // Multiply by e^{-j2πf_c t} to shift spectrum down to baseband
            baseband[i] = signal[i] * std::complex<double>(
                 std::cos(2.0 * M_PI * fc * t),
                -std::sin(2.0 * M_PI * fc * t));
        }
        return baseband;
    }

    complexVector FrontEndModule::matched_filter(const complexVector& signal)
    {
        const std::vector<double> rrc = generate_rrc_();
        const int M = static_cast<int>(rrc.size()); // span_*sps_ + 1

        // Convert pmr to plain vector for overlap_add_convolve
        const std::vector<std::complex<double>> sig_plain(signal.begin(), signal.end());
        const auto filtered = overlap_add_convolve(sig_plain, rrc);

        // Combined TX + RX group delay is (M-1) samples at 4× rate.
        // Start downsampling at that offset so the first output sample aligns
        // with the first OFDM symbol, then take every sps_-th sample.
        const int delay = M - 1;
        complexVector downsampled;
        downsampled.reserve(filtered.size() / static_cast<std::size_t>(sps_));

        for (std::size_t i = static_cast<std::size_t>(delay);
             i < filtered.size();
             i += static_cast<std::size_t>(sps_))
        {
            downsampled.push_back(filtered[i]);
        }

        return downsampled;
    }
}
