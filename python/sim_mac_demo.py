#!/usr/bin/env python3
"""
Two-station DCF demo over SimMedium.

1. Inject a busy burst so station A must wait (CCA).
2. A sends N MSDUs to B; B ACKs via MinimalDcf.
3. Print success / retry stats.

Default delivery is AWGN at high SNR (PHY-backed). Pass --bernoulli for a
fast MAC-only path (p_loss=0 unless --p-loss is set).

Run from the python/ directory:
    python sim_mac_demo.py
    python sim_mac_demo.py --bernoulli
"""

from __future__ import annotations

import argparse
import sys

from mac.constants import DIFS_US, SIFS_US, SLOT_US
from mac.frame import pack_data, parse_mpdu, bytes_to_bits, bits_to_bytes
from mac.medium import DeliveryMode, SimMedium
from mac.phy_sap import PhySap
from mac.station import MacStation, addr_from_int


def _smoke_frame_phy(phy: PhySap) -> None:
    """Sanity-check MPDU ↔ PHY round-trip in isolation (no DCF)."""
    mpdu = pack_data(addr_from_int(2), addr_from_int(1), seq=1, payload=b"hi")
    samples, airtime = phy.encode(bytes_to_bits(mpdu))
    # Ideal channel (no noise) — decode should recover bits
    bits = phy.decode(samples)
    assert bits is not None, "PHY decode failed on noiseless samples"
    back = bits_to_bytes(bits)
    frame = parse_mpdu(back)
    assert frame is not None and frame.payload == b"hi", "FCS/payload mismatch"
    print(f"[smoke] frame+PHY OK  airtime={airtime:.1f} µs  samples={len(samples)}")


def run_demo(
    *,
    n_msdu: int = 3,
    busy_us: float = 200.0,
    mode: DeliveryMode = DeliveryMode.AWGN,
    snr_db: float = 30.0,
    p_loss: float = 0.0,
    seed: int = 1,
) -> None:
    phy = PhySap()
    _smoke_frame_phy(phy)

    medium = SimMedium(
        phy,
        mode=mode,
        snr_db=snr_db,
        p_loss=p_loss,
        seed=seed,
    )

    addr_a = addr_from_int(1)
    addr_b = addr_from_int(2)
    sta_a = MacStation(addr_a, medium, phy)
    sta_b = MacStation(addr_b, medium, phy)

    print(
        f"[demo] DIFS={DIFS_US} µs  SLOT={SLOT_US} µs  SIFS={SIFS_US} µs  "
        f"mode={mode.value}"
    )

    # 1) Exogenous busy — A enqueues while channel is busy, must wait for idle.
    medium.occupy_until(medium.now_us + busy_us)
    print(f"[demo] channel busy until t={busy_us:.0f} µs (CCA should defer)")

    for i in range(n_msdu):
        sta_a.send(addr_b, f"msg-{i}".encode())

    # Drive the event loop long enough for DIFS/backoff/TX/ACK exchanges.
    medium.run(until_idle=True, max_time_us=5_000_000.0)

    got = sta_b.recv()
    print(f"[demo] t_end={medium.now_us:.1f} µs")
    print(f"[demo] B received {len(got)}/{n_msdu} MSDUs: {got}")
    print(f"[demo] A stats: {sta_a.dcf.stats}")
    print(f"[demo] B stats: {sta_b.dcf.stats}")

    if len(got) != n_msdu:
        print("[demo] WARNING: not all MSDUs delivered — check SNR / DCF / retries")
        sys.exit(1)
    if medium.now_us < busy_us:
        print("[demo] WARNING: finished before busy ended — CCA may not have deferred")
        sys.exit(1)
    print("[demo] OK")


def main() -> None:
    p = argparse.ArgumentParser(description="802.11 DCF MAC simulation demo")
    p.add_argument("-n", "--n-msdu", type=int, default=3)
    p.add_argument("--busy-us", type=float, default=200.0)
    p.add_argument("--bernoulli", action="store_true", help="skip PHY; use drop model")
    p.add_argument("--p-loss", type=float, default=0.0)
    p.add_argument("--snr-db", type=float, default=30.0)
    p.add_argument("--seed", type=int, default=1)
    args = p.parse_args()

    mode = DeliveryMode.BERNOULLI if args.bernoulli else DeliveryMode.AWGN
    run_demo(
        n_msdu=args.n_msdu,
        busy_us=args.busy_us,
        mode=mode,
        snr_db=args.snr_db,
        p_loss=args.p_loss,
        seed=args.seed,
    )


if __name__ == "__main__":
    main()
