from bladerf import _bladerf as bladerf
import numpy as np
import threading
import matplotlib.pyplot as plt

from config import ModulationTypes, CodingRates, LinkSettings
from tx_chain import generate_tx_signal
from rx_chain import receive
from modulation_module import map_bits_to_constellation

# --- Packet parameters ---
MODULATION = ModulationTypes.BPSK
CODING_RATE = CodingRates.R12
NUM_BITS = 1024          # PSDU payload bits (must be multiple of 8)
SCRAMBLER_SEED = 0x5D
SAMPLE_RATE = 20e6       # 802.11a baseband rate (not tunable in this stack)
PAD_FRONT = 2048       # RX samples captured before the scheduled TX burst
PAD_BACK = 2048         # RX samples captured after the scheduled TX burst

# --- TX/RX timing coordination ---
# TX and RX run as independent host threads, so there is no guarantee that
# sync_tx()/sync_rx() actually start moving samples at the same wall-clock
# instant: Python thread scheduling jitter and the (asymmetric, multi-ms)
# first-call USB stream spin-up cost for each direction can easily exceed the
# whole burst duration. Left alone, that means the TX burst can fire before
# RX has actually started capturing (or after it has stopped), so RX would
# only ever see noise -- independent of RF gain/antenna setup.
#
# Fix: use the bladeRF's own hardware sample-timestamp counters (one per
# direction, both driven by the same sample clock) to schedule the TX burst
# and the RX capture window at explicit FPGA sample counts, via the
# SC16_Q11_META streaming format. The FPGA -- not the host -- then gates
# when samples actually go out/come in, so host scheduling jitter no longer
# matters as long as SCHEDULE_LEAD_MS comfortably exceeds it.
SCHEDULE_LEAD_MS = 50    # how far in the future (from "now") to schedule the burst
LEAD_SAMPLES = int(SCHEDULE_LEAD_MS * 1e-3 * SAMPLE_RATE)

# BLADERF_META_FLAG_* bit values (libbladeRF.h). Not exposed by this version
# of the Python bindings (bladerf==2.5.0), so they're hard-coded here.
META_FLAG_TX_BURST_START = 1 << 0
META_FLAG_TX_BURST_END = 1 << 1

# --- Open device ---
d = bladerf.BladeRF()

# --- Configure RX1 ---
# NOTE: AGC (GainMode.Default -> SlowAttack_AGC on bladeRF 2.0) needs time to
# settle and is meant for continuously-present signals. A single ~400us burst
# capture never gives it a chance to lock, so it just cranks gain toward max
# trying to find something -- which looks like full-scale noise the whole
# time, independent of when TX actually fires. Use Manual gain for bursts.
RX_GAIN = 30   # dB, manual gain range is roughly -15..60 on bladeRF 2.0 -- tune this
TX_GAIN = 10   # dB, manual gain range is roughly -15..60 on bladeRF 2.0 -- tune this

rx_ch = d.Channel(bladerf.CHANNEL_RX(0))
rx_ch.frequency = 900e6
rx_ch.sample_rate = SAMPLE_RATE
rx_ch.bandwidth = 20e6
rx_ch.gain_mode = bladerf.GainMode.Manual
rx_ch.gain = RX_GAIN

# --- Configure TX1 ---
tx_ch = d.Channel(bladerf.CHANNEL_TX(0))
tx_ch.frequency = 900e6
tx_ch.sample_rate = SAMPLE_RATE
tx_ch.bandwidth = 20e6
tx_ch.gain = TX_GAIN

# --- Generate 802.11a BPSK R=1/2 packet ---
rng = np.random.default_rng(42)
tx_bits = rng.integers(0, 2, size=NUM_BITS)
tx_signal, link = generate_tx_signal(
    tx_bits, MODULATION, CODING_RATE, SCRAMBLER_SEED
)

# Normalize peak to ~full-scale SC16_Q11 (±2047)
peak = np.max(np.abs(tx_signal))
tx_scaled = (tx_signal / peak * 2047).astype(np.complex64)
tx_num_samples = len(tx_scaled)

# RX captures a window around the scheduled TX burst; TX itself only ever
# transmits the real packet (no padding) -- the "quiet" front/back margin is
# achieved by scheduling the RX capture to start PAD_FRONT samples before the
# TX burst's hardware timestamp, not by literally sending zeros.
num_samples = PAD_FRONT + tx_num_samples + PAD_BACK
packet_start = PAD_FRONT
packet_end = PAD_FRONT + tx_num_samples

# Purely for plotting/comparison -- reconstructs where the burst is *expected*
# to land in the RX capture, since TX itself no longer sends this padding.
tx_padded = np.concatenate([
    np.zeros(PAD_FRONT, dtype=np.complex64),
    tx_scaled,
    np.zeros(PAD_BACK, dtype=np.complex64),
])


def to_sc16q11(samples: np.ndarray) -> np.ndarray:
    iq = np.empty(2 * len(samples), dtype=np.int16)
    iq[0::2] = np.clip(samples.real, -2048, 2047).astype(np.int16)
    iq[1::2] = np.clip(samples.imag, -2048, 2047).astype(np.int16)
    return iq


tx_buf = to_sc16q11(tx_scaled)

# --- Set up sync streams ---
# SC16_Q11_META (instead of plain SC16_Q11) enables hardware-timestamped
# scheduling of TX bursts and RX capture windows; see the SCHEDULE_LEAD_MS
# comment above for why this is needed.
d.sync_config(
    layout=bladerf.ChannelLayout.RX_X1,
    fmt=bladerf.Format.SC16_Q11_META,
    num_buffers=16,
    buffer_size=8192,
    num_transfers=8,
    stream_timeout=3500,
)
d.sync_config(
    layout=bladerf.ChannelLayout.TX_X1,
    fmt=bladerf.Format.SC16_Q11_META,
    num_buffers=16,
    buffer_size=8192,
    num_transfers=8,
    stream_timeout=3500,
)

rx_ch.enable = True
tx_ch.enable = True

rx_samples = np.zeros(2 * num_samples, dtype=np.int16)


def get_timestamp(direction: "bladerf.Direction") -> int:
    """Read the FPGA's free-running sample counter for one direction."""
    ts_ptr = bladerf.ffi.new("bladerf_timestamp *")
    ret = bladerf.libbladeRF.bladerf_get_timestamp(d.dev[0], direction.value, ts_ptr)
    bladerf._check_error(ret)
    return ts_ptr[0]


# Snapshot both directions' hardware counters back-to-back (they're driven by
# the same sample clock, so a common LEAD_SAMPLES offset from "now" lands at
# the same real instant on both), then schedule TX to fire PAD_FRONT samples
# after RX starts capturing -- deterministically, regardless of host jitter.
rx_now_ts = get_timestamp(bladerf.Direction.RX)
tx_now_ts = get_timestamp(bladerf.Direction.TX)
rx_start_ts = rx_now_ts + LEAD_SAMPLES
tx_fire_ts = tx_now_ts + LEAD_SAMPLES + PAD_FRONT

rx_meta = bladerf.ffi.new("struct bladerf_metadata *", {
    "timestamp": rx_start_ts,
    "flags": 0,   # 0 => read starting at metadata.timestamp (scheduled, not "now")
})
tx_meta = bladerf.ffi.new("struct bladerf_metadata *", {
    "timestamp": tx_fire_ts,
    "flags": META_FLAG_TX_BURST_START | META_FLAG_TX_BURST_END,
})


thread_errors: list[BaseException] = []


def _guarded(fn):
    def wrapper():
        try:
            fn()
        except BaseException as exc:  # noqa: BLE001 - re-raised on main thread below
            thread_errors.append(exc)
    return wrapper


@_guarded
def do_tx():
    d.sync_tx(tx_buf.tobytes(), tx_num_samples, meta=tx_meta)


@_guarded
def do_rx():
    d.sync_rx(rx_samples, num_samples, meta=rx_meta)


tx_thread = threading.Thread(target=do_tx)
rx_thread = threading.Thread(target=do_rx)

print(
    f"TX packet: {tx_num_samples} samples, "
    f"RX capture: pad_front={PAD_FRONT}, pad_back={PAD_BACK}, "
    f"total window: {num_samples} samples"
)
print(f"Modulation={MODULATION}  coding={CODING_RATE}  bits={NUM_BITS}")
print(f"Requested RX gain={RX_GAIN} dB -> actual={rx_ch.gain} dB")
print(f"Requested TX gain={TX_GAIN} dB -> actual={tx_ch.gain} dB")
print(
    f"Scheduling: lead={SCHEDULE_LEAD_MS} ms ({LEAD_SAMPLES} samples), "
    f"rx_start_ts={rx_start_ts}, tx_fire_ts={tx_fire_ts}"
)

rx_thread.start()
tx_thread.start()
tx_thread.join()
rx_thread.join()

if thread_errors:
    if any(isinstance(e, bladerf.TimePastError) for e in thread_errors):
        raise RuntimeError(
            "Scheduled TX/RX timestamp was already in the past by the time "
            "the stream came up -- increase SCHEDULE_LEAD_MS and try again."
        ) from thread_errors[0]
    raise thread_errors[0]

rx_ch.enable = False
tx_ch.enable = False
d.close()

# Convert received samples back to complex
rx_complex = (
    rx_samples[0::2].astype(np.float32) + 1j * rx_samples[1::2].astype(np.float32)
)

# Remove RX DC offset (LO leakage / IQ bias)
dc_offset = np.mean(rx_complex)
rx_complex = rx_complex - dc_offset
print(f"RX DC offset: {dc_offset.real:.2f} + {dc_offset.imag:.2f}j")
print("RX power:", float(np.mean(np.abs(rx_complex) ** 2)))

# Sanity checks to help pick RX_GAIN empirically:
noise_region = np.concatenate([rx_complex[:packet_start], rx_complex[packet_end:]])
noise_floor = float(np.mean(np.abs(noise_region)))
burst_region = rx_complex[packet_start:packet_end]
burst_level = float(np.mean(np.abs(burst_region))) if burst_region.size else 0.0
clipped_frac = float(np.mean(np.abs(rx_complex) >= 2040))
print(f"RX noise floor (outside TX window): {noise_floor:.1f}")
print(f"RX level during TX window        : {burst_level:.1f}  "
      f"(ratio to noise floor: {burst_level / max(noise_floor, 1e-6):.2f}x)")
print(f"RX samples near full-scale (>=2040): {clipped_frac * 100:.2f}%")
if clipped_frac > 0.01:
    print("  -> RX is clipping. Lower RX_GAIN (and/or TX_GAIN if TX is overdriving RX).")
elif burst_level < 1.5 * noise_floor:
    print("  -> Burst is barely/not above the noise floor. Raise RX_GAIN or TX_GAIN, "
          "or move antennas closer / check antenna connections.")
else:
    print("  -> Burst appears distinguishable from the noise floor. Good sign.")

# --- Decode RX ---
sync = None
equalized = None
ber = None
bit_errors = None
n_compare = None
decode_error = None

try:
    rx_bits, sync, equalized = receive(
        rx_complex, MODULATION, CODING_RATE, SCRAMBLER_SEED, link
    )
    n_compare = min(len(tx_bits), len(rx_bits))
    bit_errors = int(np.sum(tx_bits[:n_compare] != rx_bits[:n_compare]))
    ber = bit_errors / n_compare if n_compare else 1.0
    print(f"Packet start : {sync.packet_start}")
    print(f"CFO          : {sync.cfo_hz:.2f} Hz")
    print(f"Bits compared: {n_compare}")
    print(f"Bit errors   : {bit_errors}")
    print(f"BER          : {ber:.6e}")
except Exception as exc:
    decode_error = exc
    print(f"Decode failed: {exc}")


def ideal_constellation(modulation: str) -> np.ndarray:
    n_bpsc = LinkSettings().bitPerSubcarrier[modulation]
    bits = []
    for key in range(2 ** n_bpsc):
        bits.extend([(key >> (n_bpsc - 1 - b)) & 1 for b in range(n_bpsc)])
    return np.array(map_bits_to_constellation(bits, modulation, n_bpsc), dtype=complex)


# --- Plots ---
t_us = np.arange(num_samples) / SAMPLE_RATE * 1e6
zoom_start = max(0, packet_start - 256)
zoom_stop = min(num_samples, packet_end + 256)
t_zoom = t_us[zoom_start:zoom_stop]

fig, axes = plt.subplots(2, 2, figsize=(13, 9))

# Full capture magnitude
axes[0, 0].plot(t_us, np.abs(tx_padded), label="TX |s|", alpha=0.85)
axes[0, 0].plot(t_us, np.abs(rx_complex), label="RX |s|", alpha=0.7)
axes[0, 0].axvline(t_us[packet_start], color="C2", linestyle="--", linewidth=1, label="TX start")
axes[0, 0].axvline(t_us[packet_end - 1], color="C2", linestyle=":", linewidth=1, label="TX end")
if sync is not None:
    axes[0, 0].axvline(
        t_us[min(max(sync.packet_start, 0), num_samples - 1)],
        color="C3", linestyle="--", linewidth=1, label="RX pkt start",
    )
axes[0, 0].set_title("TX vs RX Magnitude (full capture)")
axes[0, 0].set_xlabel("Time (µs)")
axes[0, 0].set_ylabel("|s|")
axes[0, 0].legend(loc="upper right", fontsize=8)
axes[0, 0].grid(True, alpha=0.4)

# Zoomed I/Q around TX packet window
axes[0, 1].plot(t_zoom, tx_padded.real[zoom_start:zoom_stop], label="TX I", alpha=0.8)
axes[0, 1].plot(t_zoom, tx_padded.imag[zoom_start:zoom_stop], label="TX Q", alpha=0.8)
axes[0, 1].plot(t_zoom, rx_complex.real[zoom_start:zoom_stop], label="RX I", alpha=0.7)
axes[0, 1].plot(t_zoom, rx_complex.imag[zoom_start:zoom_stop], label="RX Q", alpha=0.7)
axes[0, 1].set_title("TX vs RX I/Q (around TX packet)")
axes[0, 1].set_xlabel("Time (µs)")
axes[0, 1].set_ylabel("Amplitude")
axes[0, 1].legend(loc="upper right", fontsize=8)
axes[0, 1].grid(True, alpha=0.4)

# Spectrum
tx_fft = np.fft.fftshift(np.fft.fft(tx_padded))
rx_fft = np.fft.fftshift(np.fft.fft(rx_complex))
freqs_mhz = np.fft.fftshift(np.fft.fftfreq(num_samples, d=1 / SAMPLE_RATE)) / 1e6
tx_psd = 20 * np.log10(np.abs(tx_fft) + 1e-9)
rx_psd = 20 * np.log10(np.abs(rx_fft) + 1e-9)

axes[1, 0].plot(freqs_mhz, tx_psd, label="TX", alpha=0.85)
axes[1, 0].plot(freqs_mhz, rx_psd, label="RX", alpha=0.75)
axes[1, 0].set_title("TX vs RX Spectrum")
axes[1, 0].set_xlabel("Frequency (MHz)")
axes[1, 0].set_ylabel("Magnitude (dB)")
axes[1, 0].legend()
axes[1, 0].grid(True, alpha=0.4)
axes[1, 0].set_xlim(-10, 10)

# Constellation
ideal = ideal_constellation(MODULATION)
if equalized is not None and equalized.size:
    axes[1, 1].scatter(
        equalized.real, equalized.imag,
        s=10, alpha=0.45, label="RX equalized", zorder=2,
    )
    title = (
        f"{MODULATION} {CODING_RATE} constellation\n"
        f"BER = {ber:.4e}  ({bit_errors}/{n_compare} errors)"
    )
else:
    title = f"{MODULATION} {CODING_RATE} constellation\nDecode failed: {decode_error}"
axes[1, 1].scatter(
    ideal.real, ideal.imag,
    s=90, marker="x", linewidths=2, color="C3",
    label="Ideal", zorder=3,
)
axes[1, 1].axhline(0, color="gray", linewidth=0.6)
axes[1, 1].axvline(0, color="gray", linewidth=0.6)
axes[1, 1].set_aspect("equal")
axes[1, 1].set_xlabel("In-phase")
axes[1, 1].set_ylabel("Quadrature")
axes[1, 1].set_title(title)
axes[1, 1].legend(loc="upper right")
axes[1, 1].grid(True, alpha=0.35)

plt.tight_layout()
outfile = "ota_tx_vs_rx.png"
plt.savefig(outfile, dpi=150)
print(f"Saved → {outfile}")
