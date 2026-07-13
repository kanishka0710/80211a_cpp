from dataclasses import dataclass
from math import ceil

import numpy as np
import numpy.fft as fft

from config import LinkSettings
from utils import fft_bin_from_subcarrier
from pilot_utils import PilotLFSR


@dataclass
class OFDMDemodResult:
    freqBins: np.ndarray
    pilots: np.ndarray
    referencePilots: np.ndarray


class OFDMModule:
    def __init__(self, linkSettings: LinkSettings):
        self.linkSettings = linkSettings

    def modulate(self, data: list[int]) -> np.ndarray:
        K = self.linkSettings.numSubcarriers - self.linkSettings.numPilots
        numOfdmBlocks = ceil(len(data) / K)

        lfsr = PilotLFSR()
        output = []
        for i in range(numOfdmBlocks):
            blockStart = i * K
            curBlock = np.zeros(self.linkSettings.nFFT, dtype=complex)

            prbs = lfsr.next_polarity()
            for p in range(4):
                k = self.linkSettings.pilotPositions[p]
                fftBin = fft_bin_from_subcarrier(k, self.linkSettings.nFFT)
                curBlock[fftBin] = prbs * lfsr.POLARITY[p]

            dataIdx = 0
            for k in range(-26, 27):
                if k == 0:
                    continue
                if k in self.linkSettings.pilotPositions:
                    continue
                fftBin = fft_bin_from_subcarrier(k, self.linkSettings.nFFT)
                src = blockStart + dataIdx
                if src < len(data):
                    curBlock[fftBin] = data[src]
                dataIdx += 1

            curBlock = fft.ifft(curBlock)
            curBlock = np.concatenate((curBlock[-self.linkSettings.cpLenData:], curBlock))
            output.append(curBlock)

        return np.array(output).flatten()

    def demodulate(self, data: list[complex]) -> OFDMDemodResult:
        symbol_len = self.linkSettings.nFFT + self.linkSettings.cpLenData
        # Only demodulate complete OFDM symbols (partial trailing blocks lack a full FFT).
        numOfdmBlocks = len(data) // symbol_len
        freqBins = []
        pilots = []
        referencePilots = []

        lfsr = PilotLFSR()

        for i in range(numOfdmBlocks):
            blockStart = i * symbol_len
            bodyStart = blockStart + self.linkSettings.cpLenData

            symbol = np.asarray(data[bodyStart : bodyStart + self.linkSettings.nFFT])
            freqDomain = fft.fft(symbol)
            freqBins.append(freqDomain)

            prbs = lfsr.next_polarity()
            for p in range(4):
                k = self.linkSettings.pilotPositions[p]
                fftBin = fft_bin_from_subcarrier(k, self.linkSettings.nFFT)
                pilots.append(freqDomain[fftBin])
                referencePilots.append(prbs * lfsr.POLARITY[p])

        result = OFDMDemodResult(freqBins = np.array(freqBins).flatten(), 
                                    pilots = np.array(pilots).flatten(), 
                                    referencePilots = np.array(referencePilots).flatten())

        return result
