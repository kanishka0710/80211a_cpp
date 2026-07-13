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
        coarse = self._coarse_sync(signal)
        fine = self._fine_sync(signal, coarse)
        H = self._channel_estimation(signal, fine)
        return SyncResult(packet_start=fine.packet_start, cfo_hz=fine.cfo_hz, H=H)

    def _coarse_sync(self, signal: np.ndarray) -> SyncResult:
        """Schmidl & Cox timing/CFO using the STF's 16-sample periodicity."""
        L = self.link.nFFT // 4  # = 16, the STF period
        N = len(signal) - 2 * L
        if N <= 0:
            return SyncResult(packet_start=0, cfo_hz=0.0, H=np.zeros(self.link.nFFT, dtype=complex))

        # Vectorized P(d) / R(d)
        a = signal[: N + L]
        b = signal[L : N + 2 * L]

        # Sliding correlation of length L at lag L
        P = np.correlate(a * np.conj(b[: len(a)]), np.ones(L), mode="valid")[:N]
        R = np.correlate(np.abs(b[: len(a)]) ** 2, np.ones(L), mode="valid")[:N]
        R = np.maximum(R.real, 1e-12)
        M = (np.abs(P) ** 2) / (R ** 2)

        # The STF produces a long plateau (~128 samples). Spikes in noise/data
        # are short — pick the end of the longest run above threshold.
        threshold = 0.7
        min_plateau = 48  # well below the ideal ~128, above typical false alarms
        above = np.where(M > threshold)[0]

        if above.size == 0:
            packet_start = int(np.argmax(M))
        else:
            packet_start = self._longest_plateau_end(above, min_plateau)
            if packet_start is None:
                packet_start = int(np.argmax(M))

        Fs = self.link.nFFT / self.link.T
        cfo_hz = float(np.angle(P[packet_start]) / (2 * np.pi * L / Fs))
        return SyncResult(
            packet_start=packet_start,
            cfo_hz=cfo_hz,
            H=np.zeros(self.link.nFFT, dtype=complex),
        )

    @staticmethod
    def _longest_plateau_end(above: np.ndarray, min_len: int) -> int | None:
        """Return the last index of the longest contiguous run in `above`."""
        breaks = np.where(np.diff(above) > 1)[0]
        boundaries = np.concatenate(([-1], breaks, [len(above) - 1]))
        best_len = 0
        best_end = None
        for i in range(len(boundaries) - 1):
            start_i = boundaries[i] + 1
            end_i = boundaries[i + 1]
            run_len = end_i - start_i + 1
            if run_len > best_len:
                best_len = run_len
                best_end = int(above[end_i])
        if best_len < min_len:
            return None
        return best_end

    def _fine_sync(self, signal: np.ndarray, coarse: SyncResult) -> SyncResult:
        """
        Refines timing and CFO using the LTF.

        After coarse sync, `coarse.packet_start` is the trailing edge of  the S&C
        plateau, which sits ~128 samples into the 160-sample STF. The LTF GI2
        therefore starts ~32 samples later, and LT1 starts 32 samples after that.

        Steps:
          1. Apply coarse CFO correction.
          2. Cross-correlate the known LT sequence against a search window to
             find the exact start of LT1 (fine timing).
          3. Compute fine CFO from the phase rotation between LT1 and LT2.
          4. Return refined packet_start (true preamble start) and combined CFO.
        """
        Fs = self.link.nFFT / self.link.T
        STF_LEN = self.link.nFFT // 4 * 10  # 160 samples
        LTF_CP_LEN = self.link.cpLenTraining  # 32 samples (GI2)
        LTF_SYM_LEN = self.link.nFFT  # 64 samples

        # 1. Apply coarse CFO correction
        n = np.arange(len(signal))
        corrected = signal * np.exp(-1j * 2 * np.pi * coarse.cfo_hz * n / Fs)

        # 2. Build known LT1 time-domain sequence (no CP)
        ltf_freq = np.zeros(self.link.nFFT, dtype=complex)
        for i, k in enumerate(LONG_TRAINING_SUBCARRIERS):
            ltf_freq[fft_bin_from_subcarrier(k, self.link.nFFT)] = LONG_TRAINING_VALUES[i]
        ltf_known = fftlib.ifft(ltf_freq)  # 64 samples

        # Nominal LT1 start: end of S&C plateau + 2 STF periods + GI2
        # coarse.packet_start ≈ true_start + 128, so LT1 is at +32 (GI2) further
        lt1_nominal = coarse.packet_start + 2 * (self.link.nFFT // 4) + LTF_CP_LEN

        # 3. Cross-correlate over a +/-32-sample search window.
        # Keep the window narrow enough that LT2 (64 samples later) is excluded —
        # LT1 and LT2 are identical, so a wide window can lock onto LT2 (+64 error).
        search_range = 32
        search_start = max(0, lt1_nominal - search_range)
        search_end = min(len(corrected) - LTF_SYM_LEN, lt1_nominal + search_range)

        best_corr = -1.0
        lt1_start = int(np.clip(lt1_nominal, 0, max(0, len(corrected) - LTF_SYM_LEN)))
        for d in range(search_start, search_end + 1):
            corr = np.abs(np.vdot(ltf_known, corrected[d : d + LTF_SYM_LEN]))
            # Prefer the earliest peak on ties so we do not drift toward LT2.
            if corr > best_corr + 1e-9:
                best_corr = corr
                lt1_start = d

        # 4. Fine CFO from phase difference between LT2 and LT1
        lt1 = corrected[lt1_start : lt1_start + LTF_SYM_LEN]
        lt2 = corrected[lt1_start + LTF_SYM_LEN : lt1_start + 2 * LTF_SYM_LEN]
        if len(lt1) < LTF_SYM_LEN or len(lt2) < LTF_SYM_LEN:
            # Fall back to coarse timing if the LTF window falls off the buffer.
            true_start = coarse.packet_start - (STF_LEN - 2 * (self.link.nFFT // 4))
            return SyncResult(
                packet_start=int(true_start),
                cfo_hz=coarse.cfo_hz,
                H=np.zeros(self.link.nFFT, dtype=complex),
            )

        fine_cfo = float(np.angle(np.vdot(lt1, lt2)) / (2 * np.pi * LTF_SYM_LEN / Fs))

        # True preamble start = LT1 start − GI2 − STF
        true_start = lt1_start - LTF_CP_LEN - STF_LEN

        return SyncResult(
            packet_start=true_start,
            cfo_hz=coarse.cfo_hz + fine_cfo,
            H=np.zeros(self.link.nFFT, dtype=complex),
        )

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
