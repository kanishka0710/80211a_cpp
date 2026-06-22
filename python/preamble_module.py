import numpy as np
import numpy.fft as fft
from config import LinkSettings
from utils import fft_bin_from_subcarrier

# --- STF constants (Table 95, §17.3.3) ---
SHORT_TRAINING_SUBCARRIERS = [-24, -20, -16, -12, -8, -4, 4, 8, 12, 16, 20, 24]
SHORT_TRAINING_VALUES = np.sqrt(13.0/6.0) * np.array([0, 0, 1+1j, 0, 0, 0, -1 - 1j, 0, 0, 0, 1+1j, 0, 0, 0, -1 - 1j, 0, 0, 0, -1 - 1j, 0, 0, 0, 1+1j, 0, 0, 0, 0,
0, 0, 0, -1 - 1j, 0, 0, 0, -1 - 1j, 0, 0, 0, 1+1j, 0, 0, 0, 1+1j, 0, 0, 0, 1+1j, 0, 0, 0, 1+1j, 0, 0])

SHORT_TRAINING_REPS  = 10      # 10 × 16-sample period = 160 samples

# --- LTF constants (Table 95, §17.3.3) ---
LONG_TRAINING_SUBCARRIERS = list(range(-26, 0)) + list(range(1, 27))  # 52 subcarriers
LONG_TRAINING_VALUES = [
    # k = -26 to k = -1
     1,  1, -1, -1,  1,  1, -1,  1, -1,  1,  1,  1,  1,  1,  1, -1,
    -1,  1,  1, -1,  1, -1,  1,  1,  1,  1,
    # k = 1 to k = 26  (k=0 DC is skipped)
     1, -1, -1,  1,  1, -1,  1, -1,  1, -1, -1, -1, -1, -1,  1,  1,
    -1, -1,  1, -1,  1, -1,  1,  1,  1,  1,
]
LONG_TRAINING_REPS   = 2       # 2 × 64-sample symbols
LONG_TRAINING_CP_LEN = 32      # double-length guard interval

class PreambleModule:

    def __init__(self, linkSettings: LinkSettings):
        self.linkSettings = linkSettings
    
    def generate_stf(self) -> np.ndarray:
        curBlock = np.zeros(self.linkSettings.nFFT, dtype=complex)

        for k in SHORT_TRAINING_SUBCARRIERS:
            fftBin = fft_bin_from_subcarrier(k, self.linkSettings.nFFT)
            curBlock[fftBin] = SHORT_TRAINING_VALUES[k + 26]

        curBlock = fft.ifft(curBlock)
        return np.tile(curBlock[:16], SHORT_TRAINING_REPS)
    
    def generate_ltf(self) -> np.ndarray:
        curBlock = np.zeros(self.linkSettings.nFFT, dtype=complex)
        for i, k in enumerate(LONG_TRAINING_SUBCARRIERS):
            curBlock[fft_bin_from_subcarrier(k, self.linkSettings.nFFT)] = LONG_TRAINING_VALUES[i]

        curBlock = fft.ifft(curBlock)
        cp = curBlock[-LONG_TRAINING_CP_LEN:]   # last 32 samples → GI2
        return np.concatenate([cp, curBlock, curBlock])  # 32 + 64 + 64 = 160 samples
    
    def generate_preamble(self) -> np.ndarray:
        return np.concatenate((self.generate_stf(), self.generate_ltf()))

# if __name__ == "__main__":
#     preamble_module = PreambleModule(LinkSettings())
#     stf = preamble_module.generate_stf()
#     ltf = preamble_module.generate_ltf()
#     preamble = preamble_module.generate_preamble()

#     import matplotlib.pyplot as plt
#     plt.figure()
#     plt.subplot(1, 2, 1)
#     plt.plot(np.abs(stf), label="STF")
#     plt.plot(np.abs(ltf), label="LTF")
#     plt.subplot(1, 2, 2)
#     plt.plot(np.abs(preamble), label="Preamble")
#     plt.legend()
#     plt.show()