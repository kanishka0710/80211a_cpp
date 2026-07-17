#include "phy/sync_module.h"

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
    }

    SyncResult detect_and_sync(const std::vector<std::complex<double>>& signal, const LinkSettings& linkSettings) {
        auto coarse = coarse_sync(signal, linkSettings);
        auto fine = fine_sync(signal, coarse, linkSettings);
        auto H = channel_estimation(signal, fine, linkSettings);
        return SyncResult{fine.packet_start, fine.cfo_hz, H};
    }

    // Schmidl & Cox timing/CFO estimate using the STF's 16-sample periodicity.
    SyncResult coarse_sync(const std::vector<std::complex<double>>& signal, const LinkSettings& linkSettings) {
        const int L = linkSettings.getNFFT() / 4;
        const int N = static_cast<int>(signal.size()) - 2 * L;
        if (N <= 0) {
            return SyncResult{0, 0.0, complexVector(linkSettings.getNFFT(), std::complex<double>(0.0, 0.0))};
        }

        complexVector a(signal.begin(), signal.begin() + N + L);
        complexVector b(signal.begin() + L, signal.begin() + N + 2 * L);

        complexVector x(N + L);
        std::vector<double> xr(N + L);
        for (int n = 0; n < N + L; n++) {
            x[n] = a[n] * std::conj(b[n]);
            xr[n] = std::norm(b[n]);
        }

        // Sliding-window sums of length L, updated incrementally (P/R at lag L).
        complexVector P(N);
        std::vector<double> R(N);
        std::complex<double> pSum(0.0, 0.0);
        double rSum = 0.0;
        for (int n = 0; n < L; n++) {
            pSum += x[n];
            rSum += xr[n];
        }
        P[0] = pSum;
        R[0] = rSum;
        for (int n = 1; n < N; n++) {
            pSum += x[n + L - 1] - x[n - 1];
            rSum += xr[n + L - 1] - xr[n - 1];
            P[n] = pSum;
            R[n] = rSum;
        }

        std::vector<double> M(N);
        for (int k = 0; k < N; k++) {
            const double r = std::max(R[k], 1e-12);
            M[k] = std::pow(std::abs(P[k]), 2) / (r * r);
        }

        // The STF produces a long plateau (~128 samples). Spikes in noise/data
        // are short -- pick the end of the longest run above threshold.
        constexpr double threshold = 0.7;
        constexpr int min_plateau = 48;

        std::vector<int> above;
        for (int k = 0; k < N; k++) {
            if (M[k] > threshold) above.push_back(k);
        }

        int packet_start;
        if (above.empty()) {
            packet_start = static_cast<int>(std::max_element(M.begin(), M.end()) - M.begin());
        } else {
            packet_start = longest_plateau_end(above, min_plateau);
            if (packet_start == -1) {
                packet_start = static_cast<int>(std::max_element(M.begin(), M.end()) - M.begin());
            }
        }

        const double Fs = sample_rate(linkSettings);
        const double cfo_hz = std::arg(P[packet_start]) / (2 * M_PI * L / Fs);
        return SyncResult{packet_start, cfo_hz, complexVector(linkSettings.getNFFT(), std::complex<double>(0.0, 0.0))};
    }

    // Refines timing and CFO using the LTF.
    SyncResult fine_sync(const std::vector<std::complex<double>>& signal, const SyncResult& coarse_result, const LinkSettings& linkSettings) {
        const double Fs = sample_rate(linkSettings);
        const int nFFT = linkSettings.getNFFT();
        const int L = nFFT / 4;
        const int stf_len = L * 10;              // 160 samples
        const int ltf_cp_len = linkSettings.getCPLenTraining(); // 32 samples (GI2)
        const int ltf_sym_len = nFFT;             // 64 samples

        const complexVector corrected = apply_cfo_correction(signal, coarse_result.cfo_hz, Fs);

        // Build known LT1 time-domain sequence (no CP).
        complexVector ltf_freq(nFFT, std::complex<double>(0.0, 0.0));
        for (std::size_t i = 0; i < kLongTrainingSubcarriers.size(); i++) {
            const int index = fft_bin_from_subcarrier(kLongTrainingSubcarriers[i], nFFT);
            ltf_freq[index] = std::complex<double>(static_cast<double>(kLongTrainingValues[i]), 0.0);
        }
        const complexVector ltf_known = inverse_fft(ltf_freq, nFFT);

        // Nominal LT1 start: end of S&C plateau + 2 STF periods + GI2.
        const int lt1_nominal = coarse_result.packet_start + 2 * L + ltf_cp_len;
        const int signal_len = static_cast<int>(corrected.size());
        const int max_start = std::max(0, signal_len - ltf_sym_len);

        // Cross-correlate over a +/-32-sample search window.
        constexpr int search_range = 32;
        const int search_start = std::max(0, lt1_nominal - search_range);
        const int search_end = std::min(max_start, lt1_nominal + search_range);

        double best_corr = -1.0;
        int lt1_start = std::clamp(lt1_nominal, 0, max_start);
        for (int d = search_start; d <= search_end; d++) {
            const double corr = std::abs(vdot(ltf_known, corrected.begin() + d));
            // Prefer the earliest peak on ties so we do not drift toward LT2.
            if (corr > best_corr + 1e-9) {
                best_corr = corr;
                lt1_start = d;
            }
        }

        if (lt1_start < 0 || lt1_start + 2 * ltf_sym_len > signal_len) {
            // Fall back to coarse timing if the LTF window falls off the buffer.
            const int true_start = coarse_result.packet_start - (stf_len - 2 * L);
            return SyncResult{true_start, coarse_result.cfo_hz, complexVector(nFFT, std::complex<double>(0.0, 0.0))};
        }

        const complexVector lt1(corrected.begin() + lt1_start, corrected.begin() + lt1_start + ltf_sym_len);
        const complexVector lt2(corrected.begin() + lt1_start + ltf_sym_len, corrected.begin() + lt1_start + 2 * ltf_sym_len);
        const double fine_cfo = std::arg(vdot(lt1, lt2.begin())) / (2 * M_PI * ltf_sym_len / Fs);

        // True preamble start = LT1 start - GI2 - STF.
        const int true_start = lt1_start - ltf_cp_len - stf_len;
        return SyncResult{true_start, coarse_result.cfo_hz + fine_cfo, complexVector(nFFT, std::complex<double>(0.0, 0.0))};
    }

    // Estimates the frequency-domain channel H[k] from the LTF.
    std::vector<std::complex<double>> channel_estimation(const std::vector<std::complex<double>>& signal,
        const SyncResult& fine_result, const LinkSettings& linkSettings) {
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
        const complexVector lt2(corrected.begin() + lt1_start + ltf_sym_len, corrected.begin() + lt1_start + 2 * ltf_sym_len);
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

    // Returns the last value of the longest contiguous run of consecutive indices in `above`.
    int longest_plateau_end(const std::vector<int>& above, int min_plateau) {
        if (above.empty()) return -1;

        std::vector<int> breaks;
        for (std::size_t i = 0; i + 1 < above.size(); i++) {
            if (above[i + 1] - above[i] > 1) {
                breaks.push_back(static_cast<int>(i));
            }
        }

        std::vector<int> boundaries;
        boundaries.reserve(breaks.size() + 2);
        boundaries.push_back(-1);
        boundaries.insert(boundaries.end(), breaks.begin(), breaks.end());
        boundaries.push_back(static_cast<int>(above.size()) - 1);

        int best_len = 0;
        int best_end = -1;
        for (std::size_t i = 0; i + 1 < boundaries.size(); i++) {
            const int start_i = boundaries[i] + 1;
            const int end_i = boundaries[i + 1];
            const int run_len = end_i - start_i + 1;
            if (run_len > best_len) {
                best_len = run_len;
                best_end = above[end_i];
            }
        }
        if (best_len < min_plateau) {
            return -1;
        }
        return best_end;
    }
}
