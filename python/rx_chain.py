import numpy as np

from config import LinkSettings, ModulationTypes, CodingRates
from sync_module import SyncModule, SyncResult
from ofdm_module import OFDMModule
from modulation_module import map_constellation_to_bits
from interleaver_module import deinterleave
from fec_module import perform_FEC_RX
from utils import fft_bin_from_subcarrier

PREAMBLE_LEN = 320   # 160-sample STF + 160-sample LTF


def _equalize_with_ltf(
    freq_bins: np.ndarray,
    H: np.ndarray,
    link: LinkSettings,
) -> np.ndarray:
    """
    Per-subcarrier equalization using the LTF-derived channel estimate.

    freq_bins — flattened FFT output from OFDMModule.demodulate,
                shape = (num_symbols * nFFT,)
    H         — 64-element channel estimate from _channel_estimation,
                H[b] is the complex gain at FFT bin b
    """
    nFFT       = link.nFFT
    num_blocks = freq_bins.size // nFFT
    equalized  = []

    for i in range(num_blocks):
        block = freq_bins[i * nFFT : (i + 1) * nFFT]
        for k in range(-26, 27):
            if k == 0 or k in link.pilotPositions:
                continue
            b   = fft_bin_from_subcarrier(k, nFFT)
            H_k = H[b] if np.abs(H[b]) > 1e-10 else 1.0
            equalized.append(block[b] / H_k)

    return np.array(equalized)


def receive(
    rx_signal: np.ndarray,
    modulation: str = ModulationTypes.BPSK,
    coding_rate: str = CodingRates.R12,
    scrambler_seed: int = 0x5D,
    link: LinkSettings | None = None,
) -> tuple[np.ndarray, SyncResult, np.ndarray]:
    """
    Full 802.11a receiver chain.

    rx_signal      — raw received complex baseband samples
    modulation     — ModulationTypes constant (must match TX)
    coding_rate    — CodingRates constant (must match TX)
    scrambler_seed — 7-bit seed (must match TX)
    link           — optional LinkSettings; created from modulation if None

    Returns (rx_bits, sync_result, equalized_symbols).

    Pipeline:
        sync (timing + CFO + channel estimate)
        → CFO correction
        → strip preamble
        → OFDM demodulate (CP strip + FFT)
        → LTF-based per-subcarrier equalization
        → constellation de-map (hard decision)
        → de-interleave
        → FEC RX (depuncture → Viterbi → descramble)
    """
    if link is None:
        link = LinkSettings(modulationType=modulation)

    Fs        = link.nFFT / link.T
    n_bpsc    = link.bitPerSubcarrier[modulation]
    n_data_sc = link.numSubcarriers - link.numPilots   # 48
    n_cbps    = n_bpsc * n_data_sc

    # 1. Synchronization: coarse timing → fine timing → channel estimation
    sync = SyncModule(link).detect_and_sync(rx_signal)

    # 2. Apply combined CFO correction to full signal
    n         = np.arange(len(rx_signal))
    corrected = rx_signal * np.exp(-1j * 2 * np.pi * sync.cfo_hz * n / Fs)

    # 3. Strip preamble and extract data portion
    data_start  = sync.packet_start + PREAMBLE_LEN
    data_signal = corrected[data_start:]

    # 4. OFDM demodulate: CP removal + FFT + pilot extraction
    ofdm_result = OFDMModule(link).demodulate(data_signal)

    # 5. Per-subcarrier equalization using LTF-derived H
    equalized = _equalize_with_ltf(ofdm_result.freqBins, sync.H, link)

    # 6. Hard-decision constellation de-mapping → bits
    detected_bits = map_constellation_to_bits(list(equalized), modulation, n_bpsc)

    # 7. De-interleave per OFDM symbol block
    num_symbols   = len(detected_bits) // n_cbps
    deinterleaved = np.empty(num_symbols * n_cbps, dtype=int)
    for i in range(num_symbols):
        block = detected_bits[i * n_cbps : (i + 1) * n_cbps]
        deinterleaved[i * n_cbps : (i + 1) * n_cbps] = deinterleave(
            np.array(block), n_cbps, n_bpsc
        )

    # 8. FEC RX: depuncture → Viterbi → descramble
    rx_bits = perform_FEC_RX(deinterleaved, coding_rate, scrambler_seed)

    return np.array(rx_bits, dtype=int), sync, equalized
