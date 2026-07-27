import numpy as np
from math import ceil

from config import LinkSettings, ModulationTypes, CodingRates
from fec_module import perform_FEC_data_field, compute_ndbps
from interleaver_module import interleave
from modulation_module import map_bits_to_constellation
from ofdm_module import OFDMModule
from preamble_module import PreambleModule
from signal_module import create_signal_header


def generate_tx_signal(
    bits: np.ndarray,
    modulation: str = ModulationTypes.BPSK,
    coding_rate: str = CodingRates.R12,
    scrambler_seed: int = 0x5D,
    link: LinkSettings | None = None,
) -> tuple[np.ndarray, LinkSettings]:
    """
    Full 802.11a transmitter chain.

    bits            — raw input bits (1-D int array)
    modulation      — ModulationTypes constant
    coding_rate     — CodingRates constant
    scrambler_seed  — 7-bit non-zero seed for the data scrambler
    link            — optional LinkSettings; created from modulation if None

    Returns (tx_signal, link) where tx_signal is:
        [preamble (320 samples)] + [SIGNAL field (80 samples)] + [OFDM data symbols]
    """
    if link is None:
        link = LinkSettings(modulationType=modulation)

    n_data_sc = link.numSubcarriers - link.numPilots   # 48
    n_bpsc    = link.bitPerSubcarrier[modulation]
    n_cbps    = n_bpsc * n_data_sc

    # 1. FEC: build the DATA field (SERVICE + PSDU + TAIL + PAD, 17.3.5),
    #    scramble → convolutional encode → puncture. N_PAD is solved so the
    #    coded output lands on an exact multiple of N_CBPS, so no separate
    #    padding step is needed afterward.
    n_dbps   = compute_ndbps(n_cbps, coding_rate)
    fec_bits = np.array(
        perform_FEC_data_field(bits, coding_rate, scrambler_seed, n_dbps), dtype=int
    )

    num_ofdm_symbols = len(fec_bits) // n_cbps

    # 2. Interleave (per OFDM symbol)
    interleaved = np.empty_like(fec_bits)
    for i in range(num_ofdm_symbols):
        block = fec_bits[i * n_cbps : (i + 1) * n_cbps]
        interleaved[i * n_cbps : (i + 1) * n_cbps] = interleave(block, n_cbps, n_bpsc)

    # 3. Constellation mapping
    symbols = np.array(map_bits_to_constellation(list(interleaved), modulation, n_bpsc))

    # 4. OFDM modulate (pilots + IFFT + cyclic prefix)
    ofdm     = OFDMModule(link)
    ofdm_sig = ofdm.modulate(symbols)

    # 5. Prepend preamble (STF + LTF = 320 samples)
    preamble = PreambleModule(link).generate_preamble()

    # 6. Prepend signal header (RATE + LENGTH for the DATA field that follows)
    signal_header = create_signal_header(link, coding_rate, len(bits) // 8)

    tx_signal = np.concatenate([preamble, signal_header, ofdm_sig])
    return tx_signal, link
