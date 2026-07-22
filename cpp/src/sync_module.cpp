//
// C++ port of python/sync_module.py
//

#include "phy/sync_module.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <numeric>

#include "phy/preamble_module.h"

namespace wifi80211a {
    namespace {
        double sample_rate(const LinkSettings& linkSettings) {
            return linkSettings.getNFFT() / linkSettings.getT();
        }

        complexVector apply_cfo_correction(const complexVector& signal, double cfo_hz, double Fs) {
            complexVector corrected(signal.size());
            for (std::size_t n = 0; n < signal.size(); n++) {
                const double phase = -2.0 * M_PI * cfo_hz * static_cast<double>(n) / Fs;
                corrected[n] = signal[n] * std::exp(std::complex<double>(0.0, phase));
            }
            return corrected;
        }

        // Conjugate dot product: sum(conj(a[i]) * b[i]).
        std::complex<double> vdot(const complexVector& a, complexVector::const_iterator b_begin) {
            std::complex<double> acc(0.0, 0.0);
            for (std::size_t i = 0; i < a.size(); i++) {
                acc += std::conj(a[i]) * *(b_begin + static_cast<std::ptrdiff_t>(i));
            }
            return acc;
        }

        // Builds the known LTF time-domain sequence (length nFFT) by placing the
        // BPSK training values on their subcarriers and taking the IFFT.
        complexVector known_ltf_time_domain(const LinkSettings& linkSettings) {
            const int nFFT = linkSettings.getNFFT();
            complexVector ltf_freq(nFFT, std::complex<double>(0.0, 0.0));
            for (std::size_t i = 0; i < kLongTrainingSubcarriers.size(); i++) {
                const int index = fft_bin_from_subcarrier(kLongTrainingSubcarriers[i], nFFT);
                ltf_freq[index] = std::complex<double>(static_cast<double>(kLongTrainingValues[i]), 0.0);
            }
            return inverse_fft(ltf_freq, nFFT);
        }

        // ---- Small Eigen-based helpers used for the LTF shift-matrix ----------
        Eigen::VectorXcd to_eigen(const complexVector& v) {
            return Eigen::Map<const Eigen::VectorXcd>(v.data(), static_cast<Eigen::Index>(v.size()));
        }

        // Circular shift of `s` by `shift` positions, similar to numpy.roll
        Eigen::VectorXcd roll(const Eigen::VectorXcd& s, int shift) {
            const int n = static_cast<int>(s.size());
            Eigen::VectorXcd out(n);
            for (int i = 0; i < n; i++) {
                out[i] = s[((i - shift) % n + n) % n];
            }
            return out;
        }

        // Builds a (base.size() x cols) matrix whose j-th column is roll(base, j).
        Eigen::MatrixXcd build_shift_matrix(const Eigen::VectorXcd& base, int cols) {
            Eigen::MatrixXcd S(base.size(), cols);
            for (int col = 0; col < cols; col++) {
                S.col(col) = roll(base, col);
            }
            return S;
        }

        Eigen::MatrixXcd pseudo_inverse(const Eigen::MatrixXcd& M) {
            return M.completeOrthogonalDecomposition().pseudoInverse();
        }
    }

    SyncResult detect_and_sync(const complexVector& signal, const LinkSettings& linkSettings) {
        SyncResult timing = coarse_sync(signal, linkSettings);
        const SyncResult coarse = coarse_cfo(signal, timing, linkSettings);
        const SyncResult fine = fine_sync(signal, coarse, linkSettings);

        SyncResult final_result{
            fine.packet_start, coarse.cfo_hz, complexVector(linkSettings.getNFFT(), std::complex<double>(0.0, 0.0))
        };
        final_result.H = ls_channel_estimation(signal, final_result, linkSettings);
        return final_result;
    }

    SyncResult coarse_sync(const complexVector& signal, const LinkSettings& linkSettings,
                           const double& threshold) {
        const int M = linkSettings.getNFFT() / 4;
        constexpr int L = 10;
        const int windowLength = L * M;
        const int N = static_cast<int>(signal.size()) - windowLength;
        if (N <= 0) {
            return SyncResult{0, 0.0, complexVector(linkSettings.getNFFT(), std::complex<double>(0.0, 0.0))};
        }

        complexVector P(N, std::complex<double>(0.0, 0.0));
        for (int k = 0; k < L - 1; k++) {
            const int leftStart = k * M;
            const int rightStart = (k + 1) * M;
            const int segLen = N + M;

            complexVector a(segLen);
            for (int i = 0; i < segLen; i++) {
                a[i] = signal[rightStart + i] * std::conj(signal[leftStart + i]);
            }

            std::complex<double> windowSum(0.0, 0.0);
            for (int m = 0; m < M; m++) windowSum += a[m];
            P[0] += windowSum;

            for (int n = 1; n < N; n++) {
                windowSum += a[n + M - 1] - a[n - 1];
                P[n] += windowSum;
            }
        }

        std::vector<double> signalMag(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++) {
            signalMag[i] = std::pow(std::abs(signal[i]), 2);
        }

        std::vector<double> E(N, 0.0);
        double windowSum = 0.0;
        for (int i = 0; i < windowLength; i++) windowSum += signalMag[i];
        E[0] = windowSum;
        for (int i = 1; i < N; i++) {
            windowSum += signalMag[i + windowLength - 1] - signalMag[i - 1];
            E[i] = windowSum;
        }
        for (int i = 0; i < N; i++) E[i] = std::max(E[i], 1e-12);

        std::vector<double> lambda(N);
        for (int i = 0; i < N; i++) {
            lambda[i] = std::pow(static_cast<double>(L) / (L - 1) * std::abs(P[i]) / E[i], 2);
        }

        const double peak = *std::ranges::max_element(lambda);
        const double cutoff = threshold * peak;
        int packetStart = -1;
        double bestVal = -1.0;
        for (int n = 0; n < N; n++) {
            if (lambda[n] >= cutoff && lambda[n] > bestVal) {
                bestVal = lambda[n];
                packetStart = n;
            }
        }

        return SyncResult{packetStart, 0.0, complexVector(linkSettings.getNFFT(), std::complex<double>(0.0, 0.0))};
    }

    // Morelli-Mengali coarse CFO from the L-part STF (eqs. 18-21)
    SyncResult coarse_cfo(const complexVector& signal, SyncResult& syncResult, const LinkSettings& linkSettings) {
        constexpr int L = 10;
        constexpr int H = L / 2;
        const int M = linkSettings.getNFFT() / 4;
        const int N = L * M;

        if (syncResult.packet_start < 0 ||
            syncResult.packet_start + N > static_cast<int>(signal.size())) {
            return syncResult;
        }
        const complexVector y(signal.begin() + syncResult.packet_start,
                              signal.begin() + syncResult.packet_start + N);

        const double denom = static_cast<double>(H) *
            (4.0 * H * H - 6.0 * L * H + 3.0 * L * L - 1.0);
        std::vector<double> w(H);
        for (int m = 1; m <= H; m++) {
            w[m - 1] = 3.0 * (static_cast<double>((L - m) * (L - m + 1)) - static_cast<double>(H * (L - H))) / denom;
        }

        complexVector R(H + 1, std::complex<double>(0.0, 0.0));
        for (int m = 0; m <= H; m++) {
            std::complex<double> acc(0.0, 0.0);
            const int count = N - m * M;
            for (int i = 0; i < count; i++) {
                acc += std::conj(y[i]) * y[i + m * M];
            }
            R[m] = acc / static_cast<double>(count);
        }

        std::vector<double> psi(H);
        for (int m = 1; m <= H; m++) {
            double x = std::arg(R[m]) - std::arg(R[m - 1]) + M_PI;
            x = std::fmod(x, 2.0 * M_PI);
            if (x < 0.0) x += 2.0 * M_PI;
            psi[m - 1] = x - M_PI;
        }

        double weightedSum = 0.0;
        for (int m = 0; m < H; m++) weightedSum += w[m] * psi[m];
        const double v_hat = static_cast<double>(L) / (2.0 * M_PI) * weightedSum;

        const double Fs = sample_rate(linkSettings);
        syncResult.cfo_hz = v_hat * Fs / static_cast<double>(N);
        return syncResult;
    }

    SyncResult fine_sync(const complexVector& signal, const SyncResult& coarse_result,
                         const LinkSettings& linkSettings) {
        const double Fs = sample_rate(linkSettings);
        const int nFFT = linkSettings.getNFFT();
        const int stf_len = nFFT / 4 * 10; // 160 samples
        const int ltf_cp_len = linkSettings.getCPLenTraining(); // 32 samples (GI2)
        const int ltf_sym_len = nFFT; // 64 samples

        const complexVector ltf_known = known_ltf_time_domain(linkSettings);
        const complexVector zeroH(nFFT, std::complex<double>(0.0, 0.0));

        const int lt1_nominal = coarse_result.packet_start + stf_len + ltf_cp_len;
        constexpr int search_range = 48;
        const int search_start = std::max(0, lt1_nominal - search_range);
        const int signal_len = static_cast<int>(signal.size());
        const int max_start = std::max(0, signal_len - ltf_sym_len);
        const int search_end = std::min(max_start, lt1_nominal + search_range);

        // Timing search correlates against the CFO-precompensated signal.
        const complexVector corrected = apply_cfo_correction(signal, coarse_result.cfo_hz, Fs);

        double best_corr = -1.0;
        int lt1_start = std::clamp(lt1_nominal, 0, max_start);
        for (int d = search_start; d <= search_end; d++) {
            const double corr = std::abs(vdot(ltf_known, corrected.begin() + d));
            if (corr > best_corr + 1e-9) {
                best_corr = corr;
                lt1_start = d;
            }
        }

        if (lt1_start < 0 || lt1_start + 2 * ltf_sym_len > signal_len) {
            return SyncResult{coarse_result.packet_start, coarse_result.cfo_hz, zeroH};
        }

        const complexVector lt1(signal.begin() + lt1_start, signal.begin() + lt1_start + ltf_sym_len);
        const complexVector lt2(signal.begin() + lt1_start + ltf_sym_len,
                                signal.begin() + lt1_start + 2 * ltf_sym_len);

        const double v_coarse = coarse_result.cfo_hz * (static_cast<double>(ltf_sym_len) / Fs);

        const int Kp = ltf_cp_len;
        const int rows = 2 * ltf_sym_len;

        Eigen::VectorXcd r_v(rows);
        r_v.head(ltf_sym_len) = to_eigen(lt1);
        r_v.tail(ltf_sym_len) = to_eigen(lt2);

        const Eigen::VectorXcd ltf_known_e = to_eigen(ltf_known);
        Eigen::VectorXcd s_ext(rows);
        s_ext.head(ltf_sym_len) = ltf_known_e;
        s_ext.tail(ltf_sym_len) = ltf_known_e;

        const Eigen::MatrixXcd S = build_shift_matrix(s_ext, Kp);
        const Eigen::MatrixXcd Spinv = pseudo_inverse(S);

        constexpr double F = 0.15;
        constexpr int J = 100;
        constexpr double delta = F / J;

        std::vector<double> trial_v(2 * J + 1);
        for (int i = 0; i <= 2 * J; i++) trial_v[i] = v_coarse + (i - J) * delta;

        std::vector<double> metrics(trial_v.size());
        for (std::size_t i = 0; i < trial_v.size(); i++) {
            Eigen::VectorXcd rw(rows);
            for (int n = 0; n < rows; n++) {
                const double phase = -2.0 * M_PI * trial_v[i] * n / ltf_sym_len;
                rw[n] = r_v[n] * std::exp(std::complex<double>(0.0, phase));
            }
            const Eigen::VectorXcd z = S * (Spinv * rw);
            metrics[i] = rw.dot(z).real(); // rw.dot(z) = rw^H * z
        }

        const std::size_t best_i =
            static_cast<std::size_t>(std::max_element(metrics.begin(), metrics.end()) - metrics.begin());
        double v_hat;
        if (best_i == 0 || best_i == trial_v.size() - 1) {
            v_hat = trial_v[best_i];
        } else {
            const double y0 = metrics[best_i - 1];
            const double y1 = metrics[best_i];
            const double y2 = metrics[best_i + 1];
            const double parabolaDenom = y0 - 2.0 * y1 + y2;
            const double offset = (parabolaDenom != 0.0) ? 0.5 * (y0 - y2) / parabolaDenom : 0.0;
            v_hat = trial_v[best_i] + offset * delta;
        }

        const double fine_cfo = v_hat * (Fs / ltf_sym_len); // v_hat is the TOTAL offset, not a residual.
        const int true_start = lt1_start - ltf_cp_len - stf_len;

        return SyncResult{true_start, fine_cfo, zeroH};
    }

    // Estimates the frequency-domain channel H[k] from the LTF via direct
    // division (FFT(LTF) / known LTF spectrum).
    complexVector channel_estimation(const complexVector& signal,
                                     const SyncResult& fine_result,
                                     const LinkSettings& linkSettings) {
        const double Fs = sample_rate(linkSettings);
        const int nFFT = linkSettings.getNFFT();
        const int stf_len = nFFT / 4 * 10;
        const int ltf_cp_len = linkSettings.getCPLenTraining();
        const int ltf_sym_len = nFFT;

        complexVector H(ltf_sym_len, std::complex<double>(0.0, 0.0));

        const complexVector corrected = apply_cfo_correction(signal, fine_result.cfo_hz, Fs);

        const int lt1_start = fine_result.packet_start + stf_len + ltf_cp_len;
        if (lt1_start < 0 || lt1_start + 2 * ltf_sym_len > static_cast<int>(corrected.size())) {
            return H;
        }

        const complexVector lt1(corrected.begin() + lt1_start, corrected.begin() + lt1_start + ltf_sym_len);
        const complexVector lt2(corrected.begin() + lt1_start + ltf_sym_len,
                                corrected.begin() + lt1_start + 2 * ltf_sym_len);
        const complexVector lt1_fft = fft(lt1, nFFT);
        const complexVector lt2_fft = fft(lt2, nFFT);

        complexVector lt_avg(ltf_sym_len);
        for (int i = 0; i < ltf_sym_len; i++) {
            lt_avg[i] = (lt1_fft[i] + lt2_fft[i]) / 2.0;
        }

        for (std::size_t i = 0; i < kLongTrainingSubcarriers.size(); i++) {
            const int index = fft_bin_from_subcarrier(kLongTrainingSubcarriers[i], nFFT);
            H[index] = lt_avg[index] / static_cast<double>(kLongTrainingValues[i]);
        }
        return H;
    }

    complexVector ls_channel_estimation(const complexVector& signal, const SyncResult& sync,
                                        const LinkSettings& linkSettings) {
        const double Fs = sample_rate(linkSettings);
        const int nFFT = linkSettings.getNFFT();
        const int stf_len = nFFT / 4 * 10; // 160 samples
        const int ltf_cp_len = linkSettings.getCPLenTraining(); // 32
        const int ltf_sym_len = nFFT; // 64
        const int K = ltf_cp_len;
        const int N = nFFT;

        complexVector H(ltf_sym_len, std::complex<double>(0.0, 0.0));

        const complexVector corrected = apply_cfo_correction(signal, sync.cfo_hz, Fs);

        const int lt1_start = sync.packet_start + stf_len + ltf_cp_len;
        if (lt1_start < 0 || lt1_start + 2 * ltf_sym_len > static_cast<int>(corrected.size())) {
            return H;
        }

        const complexVector lt1(corrected.begin() + lt1_start, corrected.begin() + lt1_start + ltf_sym_len);
        const complexVector lt2(corrected.begin() + lt1_start + ltf_sym_len,
                                corrected.begin() + lt1_start + 2 * ltf_sym_len);

        complexVector avg(ltf_sym_len);
        for (int i = 0; i < ltf_sym_len; i++) avg[i] = (lt1[i] + lt2[i]) / 2.0;

        const Eigen::VectorXcd s = to_eigen(known_ltf_time_domain(linkSettings));
        const Eigen::MatrixXcd S = build_shift_matrix(s, K);
        const Eigen::VectorXcd h_hat = pseudo_inverse(S) * to_eigen(avg);

        if (!h_hat.allFinite()) {
            return channel_estimation(signal, sync, linkSettings);
        }

        complexVector h_padded(N, std::complex<double>(0.0, 0.0));
        std::copy(h_hat.data(), h_hat.data() + h_hat.size(), h_padded.begin());
        return fft(h_padded, N);
    }
}
