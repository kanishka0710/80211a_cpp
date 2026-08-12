#!/usr/bin/env python3
"""
File transfer over the DCF MAC: split a file into MSDUs, send A→B, reassemble.

Each on-air MPDU is capped at 4095 octets (802.11a SIGNAL LENGTH field), so
MSDU payloads are at most 4095 − MAC header − FCS.

Default delivery is AWGN at high SNR (PHY-backed). Pass --bernoulli for a
fast MAC-only path.

Run from the python/ directory:
    python sim_mac_file_demo.py
    python sim_mac_file_demo.py --bernoulli
    python sim_mac_file_demo.py --file sample_data/sherlock_holmes_adventure_1.txt
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from mac.constants import DIFS_US, SIFS_US, SLOT_US
from mac.frame import DATA_HDR_LEN, FCS_LEN
from mac.medium import DeliveryMode, SimMedium
from mac.phy_sap import PhySap
from mac.station import MacStation, addr_from_int

# 802.11a SIGNAL LENGTH is 12 bits → PSDU (MPDU) ≤ 4095 octets.
MAX_PSDU_OCTETS = 4095
MAX_MSDU_OCTETS = MAX_PSDU_OCTETS - DATA_HDR_LEN - FCS_LEN  # 4073

DEFAULT_FILE = Path(__file__).resolve().parent / "sample_data" / "sherlock_holmes_adventure_1.txt"


def split_file(data: bytes, chunk_size: int = MAX_MSDU_OCTETS) -> list[bytes]:
    if chunk_size <= 0:
        raise ValueError("chunk_size must be positive")
    if chunk_size > MAX_MSDU_OCTETS:
        raise ValueError(
            f"chunk_size {chunk_size} exceeds max MSDU {MAX_MSDU_OCTETS} "
            f"(PSDU limit {MAX_PSDU_OCTETS})"
        )
    return [data[i : i + chunk_size] for i in range(0, len(data), chunk_size)]


def run_demo(
    *,
    path: Path,
    chunk_size: int = MAX_MSDU_OCTETS,
    busy_us: float = 200.0,
    mode: DeliveryMode = DeliveryMode.AWGN,
    snr_db: float = 50.0,
    p_loss: float = 0.0,
    seed: int = 1,
    max_time_us: float = 60_000_000.0,
) -> None:
    data = path.read_bytes()
    chunks = split_file(data, chunk_size)
    n = len(chunks)

    phy = PhySap()
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
        f"[file] path={path}  bytes={len(data)}  "
        f"chunks={n}  max_msdu={chunk_size}  "
        f"(psdu≤{MAX_PSDU_OCTETS})"
    )
    print(
        f"[file] DIFS={DIFS_US} µs  SLOT={SLOT_US} µs  SIFS={SIFS_US} µs  "
        f"mode={mode.value}"
    )

    medium.occupy_until(medium.now_us + busy_us)
    print(f"[file] channel busy until t={busy_us:.0f} µs (CCA should defer)")

    for i, chunk in enumerate(chunks):
        print(f"[file] enqueue chunk {i}/{n - 1}  len={len(chunk)}")
        sta_a.send(addr_b, chunk)

    medium.run(until_idle=True, max_time_us=max_time_us)

    got = sta_b.recv()
    reconstructed = b"".join(got)

    print(f"[file] t_end={medium.now_us:.1f} µs")
    print(f"[file] B received {len(got)}/{n} MSDUs  "
          f"({len(reconstructed)}/{len(data)} bytes)")
    print(f"[file] A stats: {sta_a.dcf.stats}")
    print(f"[file] B stats: {sta_b.dcf.stats}")

    if len(got) != n:
        print("[file] WARNING: not all MSDUs delivered — check SNR / DCF / retries")
        sys.exit(1)
    if reconstructed != data:
        print("[file] WARNING: reconstructed bytes do not match source file")
        # Help localize corruption / reordering without dumping the whole book.
        for i, (a, b) in enumerate(zip(chunks, got)):
            if a != b:
                print(f"[file] first mismatch at chunk {i} "
                      f"(tx={len(a)} rx={len(b)})")
                break
        else:
            if len(got) != len(chunks):
                print(f"[file] chunk count mismatch tx={len(chunks)} rx={len(got)}")
        sys.exit(1)
    if medium.now_us < busy_us:
        print("[file] WARNING: finished before busy ended — CCA may not have deferred")
        sys.exit(1)

    print("[file] OK — file reconstructed bit-exact")


def main() -> None:
    p = argparse.ArgumentParser(
        description="Send a file over the 802.11 DCF MAC simulation"
    )
    p.add_argument(
        "--file",
        type=Path,
        default=DEFAULT_FILE,
        help=f"path to send (default: {DEFAULT_FILE.name})",
    )
    p.add_argument(
        "--chunk-size",
        type=int,
        default=MAX_MSDU_OCTETS,
        help=f"max MSDU payload bytes (default {MAX_MSDU_OCTETS}, "
             f"PSDU cap {MAX_PSDU_OCTETS})",
    )
    p.add_argument("--busy-us", type=float, default=200.0)
    p.add_argument("--bernoulli", action="store_true", help="skip PHY; use drop model")
    p.add_argument("--p-loss", type=float, default=0.0)
    p.add_argument(
        "--snr-db",
        type=float,
        default=50.0,
        help="AWGN SNR in dB (default 50; long MSDUs need more margin than tiny demos)",
    )
    p.add_argument("--seed", type=int, default=1)
    p.add_argument(
        "--max-time-us",
        type=float,
        default=60_000_000.0,
        help="simulation time cap in µs (default 60 s of sim time)",
    )
    args = p.parse_args()

    mode = DeliveryMode.BERNOULLI if args.bernoulli else DeliveryMode.AWGN
    run_demo(
        path=args.file,
        chunk_size=args.chunk_size,
        busy_us=args.busy_us,
        mode=mode,
        snr_db=args.snr_db,
        p_loss=args.p_loss,
        seed=args.seed,
        max_time_us=args.max_time_us,
    )


if __name__ == "__main__":
    main()
