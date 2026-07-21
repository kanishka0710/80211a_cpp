from dataclasses import dataclass
import numpy as np
import numpy.fft as fftlib

from config import LinkSettings
from preamble_module import LONG_TRAINING_VALUES, LONG_TRAINING_SUBCARRIERS
from utils import fft_bin_from_subcarrier


@dataclass
class SyncResult:
    packet_start: int
    cfo_hz: float
    H: np.ndarray


class SyncModule:
    def __init__(self, link: LinkSettings):
        self.link = link

    def detect_and_sync(self, signal: np.ndarray) -> SyncResult:
        coarse = self._coarse_cfo(signal, self._coarse_sync(signal))
        fine = self._fine_sync(signal, coarse)
        final = SyncResult(packet_start=fine.packet_start, cfo_hz=coarse.cfo_hz, H=np.zeros(self.link.nFFT, dtype=complex))
        H = self._ls_channel_estimation(signal, final)
        return SyncResult(packet_start=final.packet_start, cfo_hz=final.cfo_hz, H=H)

    def _coarse_cfo(self, signal: np.ndarray, syncResult: SyncResult) -> SyncResult:
        """Morelli–Mengali coarse CFO from the L-part STF (eqs. 18–21)."""
        L = 10
        H = L // 2
        M = self.link.nFFT // 4
        N = L * M
        y = signal[syncResult.packet_start : syncResult.packet_start + N]
        if len(y) < N:
            return syncResult

        denom = H * (4 * H**2 - 6 * L * H + 3 * L**2 - 1)
        w = np.array([
            3 * ((L - m) * (L - m + 1) - H * (L - H)) / denom
            for m in range(1, H + 1)
        ])

        R = np.zeros(H + 1, dtype=complex)
        for m in range(H + 1):
            R[m] = np.sum(np.conj(y[: N - m * M]) * y[m * M : N]) / (N - m * M)

        psi = np.array([
            ((np.angle(R[m]) - np.angle(R[m - 1]) + np.pi) % (2 * np.pi)) - np.pi
             for m in range(1, H + 1)
        ])

        v_hat = L / (2 * np.pi) * np.sum(w * psi)
        Fs = self.link.nFFT / self.link.T
        syncResult.cfo_hz = float(v_hat * Fs / N)
        return syncResult

    def _coarse_sync(self, signal: np.ndarray, threshold: float = 0.7) -> SyncResult:
        """
        Coarse timing/CFO via the generalized L-part timing metric on the STF.

        Training symbol = L identical parts of M samples. Maximize

            Λ(d) = (L/(L-1) · |P(d)| / E(d))²

        where P correlates adjacent parts (weighted by b(k)=p(k)p(k+1)) and
        E is the energy over the L·M-sample window. For 802.11a STF all parts
        are identical, so p(k)=1 and b(k)=1. Peaks near the true preamble start.
        """
        M = self.link.nFFT // 4  # = 16, samples per STF period
        L = 10                   # SHORT_TRAINING_REPS
        window = L * M           # 160
        N = len(signal) - window
        if N <= 0:
            return SyncResult(packet_start=0, cfo_hz=0.0, H=np.zeros(self.link.nFFT, dtype=complex))

        # Identical STF periods → p(k)=1 → b(k)=p(k)p(k+1)=1
        b = np.ones(L - 1)

        # P(d) = Σ_{k=0}^{L-2} b(k) Σ_{m=0}^{M-1} r*(d+kM+m) · r(d+(k+1)M+m)
        P = np.zeros(N, dtype=complex)
        for k in range(L - 1):
            left = signal[k * M : k * M + N + M]
            right = signal[(k + 1) * M : (k + 1) * M + N + M]
            P += b[k] * np.correlate(right * np.conj(left), np.ones(M), mode="valid")[:N]

        # E(d) = Σ_{i=0}^{M-1} Σ_{k=0}^{L-1} |r(d+i+kM)|²
        E = np.correlate(np.abs(signal) ** 2, np.ones(window), mode="valid")[:N]
        E = np.maximum(E.real, 1e-12)

        Lambda = ((L / (L - 1)) * np.abs(P) / E) ** 2

        # Peak detection: take the max among samples above threshold·max(Λ)
        peak = float(np.max(Lambda))
        above = np.where(Lambda >= threshold * peak)[0]
        packet_start = int(above[np.argmax(Lambda[above])]) if above.size else int(np.argmax(Lambda))

        return SyncResult(
            packet_start=packet_start,
            cfo_hz=0.0,
            H=np.zeros(self.link.nFFT, dtype=complex),
        )

    def _fine_sync(self, signal: np.ndarray, coarse: SyncResult) -> SyncResult:
        Fs = self.link.nFFT / self.link.T
        STF_LEN = self.link.nFFT // 4 * 10
        LTF_CP_LEN = self.link.cpLenTraining
        LTF_SYM_LEN = self.link.nFFT

        ltf_freq = np.zeros(self.link.nFFT, dtype=complex)
        for i, k in enumerate(LONG_TRAINING_SUBCARRIERS):
            ltf_freq[fft_bin_from_subcarrier(k, self.link.nFFT)] = LONG_TRAINING_VALUES[i]
        ltf_known = fftlib.ifft(ltf_freq)

        lt1_nominal = coarse.packet_start + STF_LEN + LTF_CP_LEN
        search_range = 48
        search_start = max(0, lt1_nominal - search_range)
        search_end = min(len(signal) - LTF_SYM_LEN, lt1_nominal + search_range)

        n = np.arange(len(signal))
        corrected = signal * np.exp(-1j * 2 * np.pi * coarse.cfo_hz * n / Fs)

        best_corr = -1.0
        lt1_start = int(np.clip(lt1_nominal, 0, max(0, len(signal) - LTF_SYM_LEN)))
        for d in range(search_start, search_end + 1):
            corr = np.abs(np.vdot(ltf_known, corrected[d : d + LTF_SYM_LEN]))
            if corr > best_corr + 1e-9:
                best_corr = corr
                lt1_start = d

        lt1 = signal[lt1_start : lt1_start + LTF_SYM_LEN]
        lt2 = signal[lt1_start + LTF_SYM_LEN : lt1_start + 2 * LTF_SYM_LEN]
        if len(lt1) < LTF_SYM_LEN or len(lt2) < LTF_SYM_LEN:
            return SyncResult(packet_start=int(coarse.packet_start), cfo_hz=coarse.cfo_hz,
                            H=np.zeros(self.link.nFFT, dtype=complex))

        v_coarse = coarse.cfo_hz * (LTF_SYM_LEN / Fs)  # normalized full coarse offset

        Kp = LTF_CP_LEN
        S = np.zeros((2 * LTF_SYM_LEN, Kp), dtype=complex)
        r_v = np.concatenate((lt1, lt2))      
        s_ext = np.concatenate((ltf_known, ltf_known))
        for col in range(Kp):
            S[:, col] = np.roll(s_ext, col)
        B = S @ np.linalg.pinv(S)

        F = 0.15
        J = 100
        delta = F / J
        trial_v = v_coarse + np.arange(-J, J + 1) * delta

        metrics = np.zeros(len(trial_v))
        idx = np.arange(2 * LTF_SYM_LEN)
        for i, v_try in enumerate(trial_v):
            W = np.exp(-1j * 2 * np.pi * v_try * idx / LTF_SYM_LEN)  # note: NEGATIVE sign to *remove* offset v_try
            rw = W * r_v
            metrics[i] = np.real(rw.conj() @ B @ rw)

        best_i = np.argmax(metrics)
        if best_i == 0 or best_i == len(trial_v) - 1:
            v_hat = trial_v[best_i]
        else:
            y0, y1, y2 = metrics[best_i - 1], metrics[best_i], metrics[best_i + 1]
            denom = (y0 - 2 * y1 + y2)
            offset = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
            v_hat = trial_v[best_i] + offset * delta

        fine_cfo = v_hat * (Fs / LTF_SYM_LEN)  # v_hat is the TOTAL offset now, not a residual
        true_start = lt1_start - LTF_CP_LEN - STF_LEN

        return SyncResult(
            packet_start=true_start,
            cfo_hz=fine_cfo,   
            H=np.zeros(self.link.nFFT, dtype=complex),
        )

    def _known_ltf_time_domain(self) -> np.ndarray:
        N = self.link.nFFT
        freq_domain = np.zeros(N, dtype=np.complex64)
        for i, k in enumerate(LONG_TRAINING_SUBCARRIERS):
            b = fft_bin_from_subcarrier(k, N)
            freq_domain[b] = LONG_TRAINING_VALUES[i]
        return fftlib.ifft(freq_domain)
    
    def _ls_channel_estimation(self, signal: np.ndarray, sync: SyncResult) -> np.ndarray:
        Fs = self.link.nFFT / self.link.T
        STF_LEN = self.link.nFFT // 4 * 10  # 160 samples
        LTF_CP_LEN = self.link.cpLenTraining  # 32
        LTF_SYM_LEN = self.link.nFFT  # 64
        K = self.link.cpLenTraining
        N = self.link.nFFT
        H = np.zeros(LTF_SYM_LEN, dtype=complex)

        # Apply full (coarse + fine) CFO correction
        n = np.arange(len(signal))
        corrected = signal * np.exp(-1j * 2 * np.pi * sync.cfo_hz * n / Fs)

        # Extract LT1 and LT2 (sync.packet_start is the true preamble start)
        lt1_start = sync.packet_start + STF_LEN + LTF_CP_LEN
        lt1 = corrected[lt1_start : lt1_start + LTF_SYM_LEN]
        lt2 = corrected[lt1_start + LTF_SYM_LEN : lt1_start + 2 * LTF_SYM_LEN]
        if len(lt1) < LTF_SYM_LEN or len(lt2) < LTF_SYM_LEN:
            return H

        s = self._known_ltf_time_domain()
        S = np.zeros((N, K), dtype=complex)
        for k in range(K):
            S[:, k] = np.roll(s, k)
        try:
            S_inv = np.linalg.pinv(S)
        except np.linalg.LinAlgError:
            return self._channel_estimation(signal, sync)
        h_hat = S_inv @ ((lt1 + lt2) / 2)
        return np.fft.fft(h_hat, n=N)

    def _channel_estimation(self, signal: np.ndarray, sync: SyncResult) -> np.ndarray:
        """
        Estimates the frequency-domain channel H[k] from the LTF.

        Applies the combined CFO correction, extracts LT1 and LT2, averages
        their FFTs for noise reduction, then divides by the known LTF sequence:

            H[k] = (FFT(LT1)[k] + FFT(LT2)[k]) / 2 / L[k]

        Returns a 64-element complex array (nFFT bins); only the 52 active
        subcarrier bins are non-zero.
        """
        Fs = self.link.nFFT / self.link.T
        STF_LEN = self.link.nFFT // 4 * 10  # 160 samples
        LTF_CP_LEN = self.link.cpLenTraining  # 32
        LTF_SYM_LEN = self.link.nFFT  # 64

        H = np.zeros(LTF_SYM_LEN, dtype=complex)

        # Apply full (coarse + fine) CFO correction
        n = np.arange(len(signal))
        corrected = signal * np.exp(-1j * 2 * np.pi * sync.cfo_hz * n / Fs)

        # Extract LT1 and LT2 (sync.packet_start is the true preamble start)
        lt1_start = sync.packet_start + STF_LEN + LTF_CP_LEN
        lt1 = corrected[lt1_start : lt1_start + LTF_SYM_LEN]
        lt2 = corrected[lt1_start + LTF_SYM_LEN : lt1_start + 2 * LTF_SYM_LEN]
        if len(lt1) < LTF_SYM_LEN or len(lt2) < LTF_SYM_LEN:
            return H

        LT_avg = (fftlib.fft(lt1) + fftlib.fft(lt2)) / 2

        # H[k] = received / known for each active subcarrier
        for i, k in enumerate(LONG_TRAINING_SUBCARRIERS):
            b = fft_bin_from_subcarrier(k, LTF_SYM_LEN)
            H[b] = LT_avg[b] / LONG_TRAINING_VALUES[i]

        return H

