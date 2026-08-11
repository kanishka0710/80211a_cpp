"""PHY service access point: MPDU bits ↔ complex baseband via existing TX/RX chains."""

from __future__ import annotations

import numpy as np

from config import CodingRates, ModulationTypes
from mac.constants import PHY_SAMPLE_RATE_HZ
from rx_chain import receive
from tx_chain import generate_tx_signal


class PhySap:
    """
    Thin wrapper around generate_tx_signal / receive.

    RATE is kept explicit (matches current receive() contract that verifies
    the SIGNAL field against caller-supplied modulation/coding).
    """

    def __init__(
        self,
        *,
        modulation: str = ModulationTypes.BPSK,
        coding_rate: str = CodingRates.R12,
        scrambler_seed: int = 0x5D,
        sample_rate_hz: float = PHY_SAMPLE_RATE_HZ,
    ) -> None:
        self.modulation = modulation
        self.coding_rate = coding_rate
        self.scrambler_seed = scrambler_seed
        self.sample_rate_hz = sample_rate_hz

    def airtime_us(self, num_samples: int) -> float:
        return 1e6 * num_samples / self.sample_rate_hz

    def encode(
        self,
        psdu_bits: np.ndarray,
        modulation: str | None = None,
        coding_rate: str | None = None,
    ) -> tuple[np.ndarray, float]:
        """
        PSDU bits → complex baseband samples and airtime in microseconds.
        PSDU length must be a multiple of 8 bits (SIGNAL LENGTH is in octets).
        """
        mod = modulation if modulation is not None else self.modulation
        cr = coding_rate if coding_rate is not None else self.coding_rate
        bits = np.asarray(psdu_bits, dtype=int).ravel()
        if len(bits) % 8 != 0:
            raise ValueError("PSDU bit length must be a multiple of 8")
        samples, _link = generate_tx_signal(
            bits, mod, cr, self.scrambler_seed
        )
        return samples, self.airtime_us(len(samples))

    def decode(
        self,
        samples: np.ndarray,
        modulation: str | None = None,
        coding_rate: str | None = None,
    ) -> np.ndarray | None:
        """
        Complex samples → PSDU bits, or None if sync/decode fails.
        FCS validation is the MAC's job (parse_mpdu); this only runs the PHY.
        """
        mod = modulation if modulation is not None else self.modulation
        cr = coding_rate if coding_rate is not None else self.coding_rate
        try:
            rx_bits, _sync, _eq = receive(
                samples, mod, cr, self.scrambler_seed
            )
        except Exception:
            return None
        return np.asarray(rx_bits, dtype=int).ravel()
