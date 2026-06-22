import numpy as np

from pilot_utils import PilotLFSR
from ofdm_module import OFDMDemodResult
from config import LinkSettings

def equalize(ofdmResult: OFDMDemodResult, linkSettings: LinkSettings):
    numOfdmBlocks = ofdmResult.freqBins.size / linkSettings.nFFT

    if ofdmResult.referencePilots.size == 0:
        pilotLfsr: PilotLFSR = PilotLFSR()
        referencePilots = np.zeros_like(ofdmResult.pilots, dtype=np.complex64)
        prbs = 1.0

        for i in range(referencePilots.size):
            if i % 4 == 0:
                pilotLfsr.next_polarity()
            referencePilots[i] = prbs * pilotLfsr.POLARITY[i % 4]
        
    equalizedSymbols = []
    for i in range(numOfdmBlocks):
        curBlock = ofdmResult.freqBins[i:i+linkSettings.nFFT]
        curPilots = ofdmResult.pilots[i:i+4]
        curRefPilots = ofdmResult.referencePilots[i:i+4]

        H = np.array(curPilots / curRefPilots)

        for sc in range(-26, 27):
            if sc == 0 or sc in linkSettings.pilotPositions:
                continue
            
            k = sc if sc > 0 else sc + linkSettings.nFFT
            H_k = 0
            if sc <= -14:
                H_k = H[0]
            elif sc <= 0:
                H_k = H[1]
            elif sc <= 14:
                H_k = H[2]
            else:
                H_k = H[3]
            
            equalizedSymbols.append(curBlock[k] / H_k)
            
    return np.array(equalizedSymbols).flatten()
            

