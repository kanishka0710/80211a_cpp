import numpy as np

from config import *
from fec_module import append_convolutional_tail, convolutional_encoder_mother, viterbi_decode
from interleaver_module import deinterleave, interleave
from modulation_module import map_bits_to_constellation, map_constellation_to_bits
from ofdm_module import OFDMModule
from utils import fft_bin_from_subcarrier, int_to_bits


def create_signal_header(
    linkSettings: LinkSettings, psduLengthOctets: int
) -> np.ndarray:
    """Builds and OFDM-modulates the 24-bit SIGNAL field (Sec. 17.3.4).

    SIGNAL is always BPSK, R=1/2, and is never scrambled or punctured,
    regardless of the RATE used for the DATA field that follows it.

    RATE is taken from linkSettings.modulationType / linkSettings.codingRate
    (same as C++ create_signal_header).
    """
    signalBits = []
    rateValue = rateMapInverse[(linkSettings.modulationType, linkSettings.codingRate)]
    signalBits.extend(int_to_bits(rateCodeword[rateValue], 4))
    signalBits.append(0)  # reserved
    signalBits.extend(int_to_bits(psduLengthOctets, 12))
    signalBits.append(sum(signalBits[:17]) % 2)  # even parity over bits 0-16
    signalBits = np.array(signalBits, dtype=np.int8)

    # append_convolutional_tail supplies the 6 zero SIGNAL TAIL bits (18-23),
    # completing the 24-bit field before the mother-rate 1/2 conv. encode.
    tailedBits = append_convolutional_tail(signalBits)
    encodedBits = np.array(convolutional_encoder_mother(tailedBits))

    # Interleave (SIGNAL is always one BPSK symbol: NCBPS=48, NBPSC=1)
    interleavedBits = interleave(encodedBits, n_cbps=48, n_bpsc=1)

    modulatedSymbols = np.array(
        map_bits_to_constellation(interleavedBits, ModulationTypes.BPSK, 1),
        dtype=np.complex64)

    # OFDM modulate (pilots + IFFT + cyclic prefix) -> one 80-sample symbol
    ofdm = OFDMModule(linkSettings)
    return ofdm.modulate(modulatedSymbols)


def decode_signal_header(
    linkSettings: LinkSettings, signalHeader: np.ndarray, H: np.ndarray | None = None
) -> tuple[str, str, int]:
    """Inverse of create_signal_header: OFDM demod -> (optional LTF equalize)
    -> BPSK demap -> deinterleave -> Viterbi decode (R=1/2, nothing punctured)
    -> RATE/LENGTH/parity.

    On success, updates linkSettings modulation/coding to the decoded RATE
    (mirrors C++ decode_signal_header).

    H — optional LTF-derived channel estimate (see sync_module); the SIGNAL
        symbol rides through the same channel as the DATA field, so it must
        be equalized the same way before demapping.

    Returns (modulationType, codingRate, psduLengthOctets) for the DATA field
    that follows this SIGNAL symbol.
    """
    ofdm = OFDMModule(linkSettings)
    demod = ofdm.demodulate(signalHeader)

    # Data-subcarrier bins in the same order create_signal_header assigned them
    dataBins = []
    for k in range(-26, 27):
        if k == 0 or k in linkSettings.pilotPositions:
            continue
        fftBin = fft_bin_from_subcarrier(k, linkSettings.nFFT)
        sample = demod.freqBins[fftBin]
        if H is not None:
            H_k = H[fftBin] if np.abs(H[fftBin]) > 1e-10 else 1.0
            sample = sample / H_k
        dataBins.append(sample)

    codedBits = map_constellation_to_bits(dataBins, ModulationTypes.BPSK, 1)
    deinterleavedBits = deinterleave(np.array(codedBits), n_cbps=48, n_bpsc=1)

    maskBits = np.ones(len(deinterleavedBits), dtype=int)
    decodedBits = viterbi_decode(deinterleavedBits, maskBits)[:18]  # drop 6 tail bits

    rateBits = decodedBits[0:4]
    lengthBits = decodedBits[5:17]
    parityBit = decodedBits[17]

    rateCodewordBits = sum(int(b) << i for i, b in enumerate(rateBits))
    if rateCodewordBits not in rateCodewordToValue:
        raise ValueError(
            f"Invalid SIGNAL RATE codeword 0b{rateCodewordBits:04b} "
            f"({rateCodewordBits}); expected one of "
            f"{sorted(f'0b{c:04b}' for c in rateCodewordToValue)}"
        )
    modulationType, codingRate = rateMap[rateCodewordToValue[rateCodewordBits]]
    psduLengthOctets = sum(int(b) << i for i, b in enumerate(lengthBits))

    expectedParity = sum(decodedBits[:17]) % 2
    if expectedParity != parityBit:
        raise ValueError("SIGNAL field parity check failed")

    linkSettings.change_modulation_type(modulationType)
    linkSettings.change_coding_rate(codingRate)

    return modulationType, codingRate, psduLengthOctets
