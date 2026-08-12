from __future__ import annotations

import heapq
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Protocol

import numpy as np

from loopback_test import add_awgn
from mac.frame import bits_to_bytes, bytes_to_bits, parse_mpdu
from mac.phy_sap import PhySap


class DeliveryMode(str, Enum):
    AWGN = "awgn"
    BERNOULLI = "bernoulli"


class MediumPeer(Protocol):
    """Anything that can receive a delivered MPDU from the medium."""

    address: bytes

    def on_medium_rx(self, mpdu: bytes) -> None: ...


@dataclass(order=True)
class _Event:
    time_us: float
    seq: int
    callback: Callable[[], None] = field(compare=False)


@dataclass
class _TxJob:
    src_addr: bytes
    dst_addr: bytes | None  # None = broadcast to all other peers
    samples: np.ndarray
    mpdu_bytes: bytes  # original MPDU (used by bernoulli mode / collision bookkeeping)
    start_us: float
    end_us: float
    collided: bool = False


class SimMedium:
    """
    Shared channel with a virtual clock and priority-queue event loop.

    Stations register as peers; DCF timers use schedule(delay_us, callback).
    """

    def __init__(
        self,
        phy: PhySap | None = None,
        *,
        mode: DeliveryMode = DeliveryMode.AWGN,
        snr_db: float = 30.0,
        p_loss: float = 0.0,
        seed: int = 0,
        max_delay_samples: int = 0,
    ) -> None:
        self.phy = phy if phy is not None else PhySap()
        self.mode = mode
        self.snr_db = snr_db
        self.p_loss = p_loss
        # Random pre-pad before decode (0 = off). Non-zero stress-tests sync but
        # raises PER on long MSDUs; keep 0 for reliable file-transfer sims.
        self.max_delay_samples = max_delay_samples
        self.rng = np.random.default_rng(seed)

        self._now_us: float = 0.0
        self._events: list[_Event] = []
        self._event_seq: int = 0
        self._peers: dict[bytes, MediumPeer] = {}

        # Exogenous + TX busy intervals: list of (start_us, end_us)
        self._busy: list[tuple[float, float]] = []
        self._active_tx: list[_TxJob] = []

    # ── Clock / events ────────────────────────────────────────────────────────

    @property
    def now_us(self) -> float:
        return self._now_us

    def register(self, peer: MediumPeer) -> None:
        self._peers[peer.address] = peer

    def schedule(self, delay_us: float, callback: Callable[[], None]) -> None:
        """Fire callback at now + delay_us (absolute time via the event queue)."""
        if delay_us < 0:
            raise ValueError("delay_us must be >= 0")
        heapq.heappush(
            self._events,
            _Event(self._now_us + delay_us, self._event_seq, callback),
        )
        self._event_seq += 1

    def schedule_at(self, time_us: float, callback: Callable[[], None]) -> None:
        if time_us < self._now_us:
            raise ValueError("cannot schedule in the past")
        heapq.heappush(
            self._events,
            _Event(time_us, self._event_seq, callback),
        )
        self._event_seq += 1

    def run_until(self, time_us: float) -> None:
        """Process events until the virtual clock reaches time_us."""
        while self._events and self._events[0].time_us <= time_us:
            ev = heapq.heappop(self._events)
            self._now_us = ev.time_us
            ev.callback()
        self._now_us = max(self._now_us, time_us)

    def run(self, until_idle: bool = True, max_time_us: float = 1e9) -> None:
        """Drain the event queue (optionally capped)."""
        limit = self._now_us + max_time_us
        while self._events and self._events[0].time_us <= limit:
            ev = heapq.heappop(self._events)
            self._now_us = ev.time_us
            ev.callback()
            if until_idle and not self._events and not self._active_tx:
                break

    # ── CCA ───────────────────────────────────────────────────────────────────

    def is_idle(self) -> bool:
        return not self.sense_busy()

    def sense_busy(self) -> bool:
        """True if now falls inside any busy interval (TX or injected)."""
        t = self._now_us
        for start, end in self._busy:
            if start <= t < end:
                return True
        return False

    def occupy_until(self, t_end: float) -> None:
        """Inject exogenous channel busy from now until t_end (DCF test knob)."""
        if t_end <= self._now_us:
            return
        self._mark_busy(self._now_us, t_end)

    def occupy(self, start_us: float, end_us: float) -> None:
        if end_us <= start_us:
            return
        self._mark_busy(start_us, end_us)

    def _mark_busy(self, start_us: float, end_us: float) -> None:
        self._busy.append((start_us, end_us))

    def _prune_busy(self) -> None:
        t = self._now_us
        self._busy = [(s, e) for s, e in self._busy if e > t]

    # ── Transmit / deliver ────────────────────────────────────────────────────

    def transmit(
        self,
        src_addr: bytes,
        samples: np.ndarray,
        airtime_us: float,
        mpdu_bytes: bytes,
        *,
        dst_addr: bytes | None = None,
        start_us: float | None = None,
    ) -> float:
        """
        Start a transmission at start_us (default: now).

        Marks the medium busy for airtime_us. On completion, delivers to
        dst (or all other peers if dst_addr is None) unless collided/dropped.

        Returns the scheduled end time (µs).
        """
        if airtime_us <= 0:
            raise ValueError("airtime_us must be > 0")
        start = self._now_us if start_us is None else start_us
        end = start + airtime_us

        job = _TxJob(
            src_addr=src_addr,
            dst_addr=dst_addr,
            samples=np.asarray(samples),
            mpdu_bytes=mpdu_bytes,
            start_us=start,
            end_us=end,
        )

        # Collision: overlap with any already-scheduled TX
        for other in self._active_tx:
            if _intervals_overlap(job.start_us, job.end_us, other.start_us, other.end_us):
                job.collided = True
                other.collided = True

        self._active_tx.append(job)
        self._mark_busy(start, end)

        def _on_end() -> None:
            self._finish_tx(job)

        self.schedule_at(end, _on_end)
        return end

    def _finish_tx(self, job: _TxJob) -> None:
        if job in self._active_tx:
            self._active_tx.remove(job)
        self._prune_busy()

        if job.collided:
            # Collision: both frames lost; no RX indication (sender relies on ACK timeout).
            return

        mpdu = self._decode_for_delivery(job)
        if mpdu is None:
            return
        self._deliver(job, mpdu=mpdu)

    def _decode_for_delivery(self, job: _TxJob) -> bytes | None:
        if self.mode == DeliveryMode.BERNOULLI:
            if self.rng.random() < self.p_loss:
                return None
            return job.mpdu_bytes

        # AWGN path through the real PHY
        noisy = add_awgn(job.samples, self.snr_db, self.rng)
        if self.max_delay_samples > 0:
            delay = int(self.rng.integers(0, self.max_delay_samples + 1))
            if delay > 0:
                pad = (
                    self.rng.standard_normal(delay)
                    + 1j * self.rng.standard_normal(delay)
                ) / np.sqrt(2)
                noisy = np.concatenate([pad, noisy])

        bits = self.phy.decode(noisy)
        if bits is None:
            return None
        raw = bits_to_bytes(bits)
        # FCS check: only deliver if it parses as a valid MPDU
        if parse_mpdu(raw) is None:
            return None
        return raw

    def _deliver(self, job: _TxJob, *, mpdu: bytes) -> None:
        targets: list[MediumPeer]
        if job.dst_addr is not None:
            peer = self._peers.get(job.dst_addr)
            targets = [peer] if peer is not None else []
        else:
            targets = [p for a, p in self._peers.items() if a != job.src_addr]

        for peer in targets:
            peer.on_medium_rx(mpdu)


def _intervals_overlap(a0: float, a1: float, b0: float, b1: float) -> bool:
    return a0 < b1 and b0 < a1


# Convenience: encode MPDU bytes → samples for callers that have PhySap
def encode_mpdu(phy: PhySap, mpdu: bytes) -> tuple[np.ndarray, float]:
    return phy.encode(bytes_to_bits(mpdu))
