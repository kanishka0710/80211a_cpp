import numpy as np

from config import LinkSettings, ModulationTypes, CodingRates
from sync_module import SyncModule, SyncResult
from ofdm_module import OFDMModule
from modulation_module import map_constellation_to_bits
from interleaver_module import deinterleave
from fec_module import perform_FEC_data_field_RX
from signal_module import decode_signal_header
from utils import fft_bin_from_subcarrier

PREAMBLE_LEN = 320   # 160-sample STF + 160-sample LTF
SIGNAL_LEN   = 80    # 64-sample FFT + 16-sample CP (1 OFDM symbol: RATE + LENGTH)


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
        → decode SIGNAL field (RATE + LENGTH) → verify against caller-supplied params
        → strip SIGNAL field
        → OFDM demodulate (CP strip + FFT)
        → LTF-based per-subcarrier equalization
        → constellation de-map (hard decision)
        → de-interleave
        → FEC RX (depuncture → Viterbi → descramble → strip SERVICE field)
        → trim to the PSDU length carried in the SIGNAL field
    """
    if link is None:
        link = LinkSettings(modulationType=modulation)

    Fs        = link.nFFT / link.T
    n_bpsc    = link.bitPerSubcarrier[modulation]
    n_data_sc = link.numSubcarriers - link.numPilots   # 48
    n_cbps    = n_bpsc * n_data_sc

    # 1. Synchronization: coarse timing → fine timing → channel estimation
    sync = SyncModule(link).detect_and_sync(rx_signal)

    # 2. Apply combined CFO correction to full signal (cfo_hz is in Hz)
    n = np.arange(len(rx_signal))
    corrected = rx_signal * np.exp(-1j * 2 * np.pi * sync.cfo_hz * n / Fs)

    # 3. Strip preamble and decode the SIGNAL field (RATE + LENGTH, always BPSK R=1/2)
    signal_start = sync.packet_start + PREAMBLE_LEN
    signal_field = corrected[signal_start : signal_start + SIGNAL_LEN]
    sig_modulation, sig_coding_rate, psdu_length_octets = decode_signal_header(
        link, signal_field, sync.H
    )
    if (sig_modulation, sig_coding_rate) != (modulation, coding_rate):
        raise ValueError(
            f"SIGNAL field mismatch: decoded ({sig_modulation}, {sig_coding_rate}) "
            f"!= expected ({modulation}, {coding_rate})"
        )

    # 4. Strip SIGNAL field, leaving the DATA field
    data_start  = signal_start + SIGNAL_LEN
    data_signal = corrected[data_start:]

    # 5. OFDM demodulate: CP removal + FFT + pilot extraction
    ofdm_result = OFDMModule(link).demodulate(data_signal)

    # 6. Per-subcarrier equalization using LTF-derived H
    equalized = _equalize_with_ltf(ofdm_result.freqBins, sync.H, link)

    # 7. Hard-decision constellation de-mapping → bits
    detected_bits = map_constellation_to_bits(list(equalized), modulation, n_bpsc)

    # 8. De-interleave per OFDM symbol block
    num_symbols   = len(detected_bits) // n_cbps
    deinterleaved = np.empty(num_symbols * n_cbps, dtype=int)
    for i in range(num_symbols):
        block = detected_bits[i * n_cbps : (i + 1) * n_cbps]
        deinterleaved[i * n_cbps : (i + 1) * n_cbps] = deinterleave(
            np.array(block), n_cbps, n_bpsc
        )

    # 9. FEC RX: depuncture → Viterbi → descramble → strip the 16-bit SERVICE field
    rx_bits = perform_FEC_data_field_RX(deinterleaved, coding_rate, scrambler_seed)

    # 10. Trim tail/pad using the PSDU length carried in the SIGNAL field
    rx_bits = np.array(rx_bits, dtype=int)[: psdu_length_octets * 8]

    return rx_bits, sync, equalized
