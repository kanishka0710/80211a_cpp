"""Plot the 802.11a TX baseband waveform written by wifi80211a_app."""

import csv
import math
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

CSV_PATH  = "waveform.csv"
NFFT      = 64
CP_LEN    = 16
SYM_LEN   = NFFT + CP_LEN   # 80 samples per OFDM symbol

def load_csv(path: str):
    samples, re, im, mag = [], [], [], []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            samples.append(int(row["sample"]))
            re.append(float(row["real"]))
            im.append(float(row["imag"]))
            mag.append(float(row["magnitude"]))
    return samples, re, im, mag

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else CSV_PATH
    samples, re, im, mag = load_csv(path)
    n_syms = math.ceil(len(samples) / SYM_LEN)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    fig.suptitle("802.11a TX — baseband waveform", fontweight="bold")

    # ── I/Q ──────────────────────────────────────────────────────────────────
    ax1.plot(samples, re, lw=1.2, label="I (real)")
    ax1.plot(samples, im, lw=1.2, label="Q (imag)", alpha=0.8)
    for b in range(SYM_LEN, len(samples), SYM_LEN):
        ax1.axvline(b, color="gray", lw=0.7, ls="--")
    ax1.set_ylabel("Amplitude")
    ax1.set_title(f"I/Q  ({n_syms} OFDM symbols, dashed = symbol boundary)")
    ax1.legend(loc="upper right")
    ax1.grid(True, alpha=0.4)

    # Shade each cyclic prefix
    for s in range(n_syms):
        start = s * SYM_LEN
        ax1.axvspan(start, start + CP_LEN, alpha=0.08, color="orange")

    # ── Envelope ─────────────────────────────────────────────────────────────
    ax2.plot(samples, mag, lw=1.2, color="C2")
    for b in range(SYM_LEN, len(samples), SYM_LEN):
        ax2.axvline(b, color="gray", lw=0.7, ls="--")
    ax2.set_ylabel("|s(n)|")
    ax2.set_xlabel("Sample index")
    ax2.set_title("Envelope  (orange = cyclic prefix)")
    ax2.grid(True, alpha=0.4)
    for s in range(n_syms):
        start = s * SYM_LEN
        ax2.axvspan(start, start + CP_LEN, alpha=0.08, color="orange")

    ax1.xaxis.set_major_locator(ticker.MultipleLocator(SYM_LEN))
    fig.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()
