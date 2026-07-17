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

def add_cfo_and_phase(signal, cfo_hz, sample_rate, phase_rad=None):
    if phase_rad is None:
        phase_rad = np.random.uniform(-np.pi, np.pi)
    n = np.arange(len(signal))
    return signal * np.exp(1j * (2*np.pi*cfo_hz*n/sample_rate + phase_rad))

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
    rx_signal = add_cfo_and_phase(add_delay(add_awgn(tx_signal, snr_db, rng), delay, rng), 50e3, 20e6)

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

BEST_ERROR_PATH = "best_error.npy"

if __name__ == "__main__":
    NUM_BITS  = 1024
    SNR_RANGE = np.linspace(0, 30, 15)

    CONFIGS = [
        (ModulationTypes.BPSK,  CodingRates.R12),
        (ModulationTypes.BPSK,  CodingRates.R23),
        (ModulationTypes.BPSK,  CodingRates.R34),
        (ModulationTypes.QPSK,  CodingRates.R12),
        (ModulationTypes.QPSK,  CodingRates.R23),
        (ModulationTypes.QPSK,  CodingRates.R34),
        (ModulationTypes.QAM16, CodingRates.R12),
        (ModulationTypes.QAM16, CodingRates.R23),
        (ModulationTypes.QAM16, CodingRates.R34),
        (ModulationTypes.QAM64, CodingRates.R12),
        (ModulationTypes.QAM64, CodingRates.R23),
        (ModulationTypes.QAM64, CodingRates.R34),
    ]

    fig, ax = plt.subplots(figsize=(9, 6))
    results  = {}
    colors = {
        ModulationTypes.BPSK: "blue",
        ModulationTypes.QPSK: "red",
        ModulationTypes.QAM16: "green",
        ModulationTypes.QAM64: "purple",
    }
    linestyles = {
        CodingRates.R12: "-",
        CodingRates.R23: "--",
        CodingRates.R34: "-.",
    }

    all_bers = []
    for mod, rate in CONFIGS:
        label = f"{mod} {rate}"
        print(f"\n── {label} ──")
        bers = snr_sweep(NUM_BITS, mod, rate, SNR_RANGE)
        results[label] = bers
        all_bers.extend(bers)
        ax.semilogy(SNR_RANGE, [max(b, 1e-5) for b in bers], marker="o", label=label, color=colors[mod], linestyle=linestyles[rate])

    avg_ber = float(np.mean(all_bers)) if all_bers else 0.0
    print(f"\nAverage BER: {avg_ber:.4f}")

    try:
        best_error = float(np.load(BEST_ERROR_PATH))
    except FileNotFoundError:
        best_error = None

    if best_error is None or avg_ber < best_error:
        np.save(BEST_ERROR_PATH, np.array(avg_ber))
        if best_error is None:
            print(f"Saved new best → {BEST_ERROR_PATH} ({avg_ber:.6e})")
        else:
            print(f"New best! {avg_ber:.6e} < {best_error:.6e}  → saved {BEST_ERROR_PATH}")
    else:
        print(f"No improvement ({avg_ber:.6e} >= best {best_error:.6e})")

    ax.set_xlabel("SNR (dB)")
    ax.set_ylabel("BER")
    ax.set_title("802.11a Loopback — BER vs SNR  (AWGN)")
    ax.legend()
    ax.grid(True, which="both", alpha=0.4)
    ax.set_ylim(1e-6, 1.0)

    plt.tight_layout()
    plt.savefig("ber_vs_snr.png", dpi=150)
    plt.show()
    print("\nSaved → ber_vs_snr.png")
