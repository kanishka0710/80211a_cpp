"""
Single-packet loopback: print BER and plot the equalized RX constellation.
"""

import numpy as np
import matplotlib.pyplot as plt

from config import ModulationTypes, CodingRates, LinkSettings
from loopback_test import run_trial
from modulation_module import map_bits_to_constellation

modulation = ModulationTypes.QAM16
coding_rate = CodingRates.R23
snr_db = 20
num_bits = 10000
seed = 42


def ideal_constellation(modulation: str) -> np.ndarray:
    """Ideal TX constellation points for the given modulation."""
    n_bpsc = LinkSettings().bitPerSubcarrier[modulation]
    # All bit patterns 0 .. 2^n_bpsc - 1, packed MSB-first
    bits = []
    for key in range(2 ** n_bpsc):
        bits.extend([(key >> (n_bpsc - 1 - b)) & 1 for b in range(n_bpsc)])
    return np.array(map_bits_to_constellation(bits, modulation, n_bpsc), dtype=complex)


if __name__ == "__main__":
    result = run_trial(num_bits, modulation, coding_rate, snr_db, seed=seed)

    print(f"Modulation : {modulation}")
    print(f"Coding rate: {coding_rate}")
    print(f"SNR        : {snr_db} dB")
    print(f"Bits       : {result['bits_compared']}")
    print(f"Bit errors : {result['bit_errors']}")
    print(f"BER        : {result['ber']:.6e}")
    print(f"Packet start: {result['packet_start']}  (true delay={result['true_delay']})")
    print(f"CFO        : {result['cfo_hz']:.2f} Hz")

    equalized = result["equalized"]
    ideal = ideal_constellation(modulation)

    fig, ax = plt.subplots(figsize=(7, 7))
    ax.scatter(
        equalized.real, equalized.imag,
        s=8, alpha=0.45, label="RX equalized", zorder=2,
    )
    ax.scatter(
        ideal.real, ideal.imag,
        s=80, marker="x", linewidths=2, color="C3",
        label="Ideal", zorder=3,
    )
    ax.axhline(0, color="gray", linewidth=0.6)
    ax.axvline(0, color="gray", linewidth=0.6)
    ax.set_aspect("equal")
    ax.set_xlabel("In-phase")
    ax.set_ylabel("Quadrature")
    ax.set_title(
        f"{modulation} constellation @ SNR={snr_db} dB\n"
        f"BER = {result['ber']:.4e}  ({result['bit_errors']}/{result['bits_compared']} errors)"
    )
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.35)

    plt.tight_layout()
    outfile = f"constellation_{modulation}_{snr_db}dB.png"
    plt.savefig(outfile, dpi=150)
    plt.show()
    print(f"\nSaved → {outfile}")
