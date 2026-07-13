"""
802.11a TX → AWGN channel → RX loopback test.

Runs BER sweeps across SNR values for every modulation / coding-rate
combination, then prints a summary table and saves a BER-vs-SNR plot.
"""

import numpy as np
import matplotlib.pyplot as plt

from config import ModulationTypes, CodingRates, LinkSettings
from tx_chain import generate_tx_signal
from rx_chain import receive


# ── Channel ───────────────────────────────────────────────────────────────────

def add_delay(
    signal: np.ndarray,
    delay_samples: int,
    rng: np.random.Generator,
) -> np.ndarray:
    """Prepend `delay_samples` of unit-power complex noise."""
    delay = (
        rng.standard_normal(delay_samples) + 1j * rng.standard_normal(delay_samples)
    ) / np.sqrt(2)
    return np.concatenate([delay, signal])

def add_awgn(signal: np.ndarray, snr_db: float, rng: np.random.Generator) -> np.ndarray:
    """Add complex AWGN scaled to the given per-sample SNR (in dB)."""
    signal_power = np.mean(np.abs(signal) ** 2)
    noise_power  = signal_power / (10 ** (snr_db / 10))
    noise = np.sqrt(noise_power / 2) * (
        rng.standard_normal(len(signal)) + 1j * rng.standard_normal(len(signal))
    )
    return signal + noise


# ── Single trial ──────────────────────────────────────────────────────────────

def run_trial(
    num_bits:      int,
    modulation:    str,
    coding_rate:   str,
    snr_db:        float,
    scrambler_seed: int = 0x5D,
    seed:          int  = 42,
) -> dict:
    """
    One TX→channel→RX trial.  Returns a dict with:
        ber, bit_errors, bits_compared,
        packet_start, cfo_hz, equalized
    """
    rng  = np.random.default_rng(seed)
    bits = rng.integers(0, 2, size=num_bits)

    tx_signal, link = generate_tx_signal(
        bits, modulation, coding_rate, scrambler_seed
    )
    # Per-SNR delay offset keeps trials reproducible while still stressing sync.
    delay = int(rng.integers(0, 235))
    rx_signal = add_delay(add_awgn(tx_signal, snr_db, rng), delay, rng)

    rx_bits, sync, equalized = receive(
        rx_signal, modulation, coding_rate, scrambler_seed, link
    )

    # Compare only the original num_bits (RX may produce tail-padding)
    n_compare  = min(num_bits, len(rx_bits))
    bit_errors = int(np.sum(bits[:n_compare] != rx_bits[:n_compare]))
    ber        = bit_errors / n_compare

    return {
        "ber":          ber,
        "bit_errors":   bit_errors,
        "bits_compared": n_compare,
        "packet_start": sync.packet_start,
        "cfo_hz":       sync.cfo_hz,
        "true_delay":   delay,
        "true_bits":    bits[:n_compare],
        "rx_bits":      rx_bits[:n_compare],
        "equalized":    equalized,
    }


# ── SNR sweep ─────────────────────────────────────────────────────────────────

def snr_sweep(
    num_bits:    int,
    modulation:  str,
    coding_rate: str,
    snr_range:   list[float],
    seed:        int = 42,
) -> list[float]:
    bers = []
    for snr_db in snr_range:
        result = run_trial(num_bits, modulation, coding_rate, snr_db, seed=seed)
        bers.append(result["ber"])
        print(
            f"  {modulation:5s} {coding_rate:3s}  SNR={snr_db:5.1f} dB  "
            f"BER={result['ber']:.4f}  errors={result['bit_errors']}/{result['bits_compared']}  "
            f"pkt_start={result['packet_start']}  cfo={result['cfo_hz']:.1f} Hz  true_delay={result['true_delay']} samples"
        )
    return bers


# ── Main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    NUM_BITS  = 1024
    SNR_RANGE = [0, 5, 10, 15, 20, 25, 30]

    CONFIGS = [
        (ModulationTypes.BPSK,  CodingRates.R12),
        (ModulationTypes.QPSK,  CodingRates.R12),
        (ModulationTypes.QAM16, CodingRates.R12),
        (ModulationTypes.QAM64, CodingRates.R12),
    ]

    fig, ax = plt.subplots(figsize=(9, 6))
    results  = {}

    for mod, rate in CONFIGS:
        label = f"{mod} {rate}"
        print(f"\n── {label} ──")
        bers = snr_sweep(NUM_BITS, mod, rate, SNR_RANGE)
        results[label] = bers
        ax.semilogy(SNR_RANGE, [max(b, 1e-5) for b in bers], marker="o", label=label)

    ax.set_xlabel("SNR (dB)")
    ax.set_ylabel("BER")
    ax.set_title("802.11a Loopback — BER vs SNR  (AWGN)")
    ax.legend()
    ax.grid(True, which="both", alpha=0.4)
    ax.set_ylim(1e-5, 1.0)

    plt.tight_layout()
    plt.savefig("ber_vs_snr.png", dpi=150)
    plt.show()
    print("\nSaved → ber_vs_snr.png")
