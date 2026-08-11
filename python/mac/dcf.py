from __future__ import annotations

from collections import deque
from enum import Enum, auto
from typing import TYPE_CHECKING, Callable

from mac.constants import (
    ACK_TIMEOUT_US,
    CW_MAX,
    CW_MIN,
    DIFS_US,
    SHORT_RETRY_LIMIT,
    SIFS_US,
    SLOT_US,
)
from mac.frame import MacFrame, pack_ack, parse_mpdu
from mac.medium import encode_mpdu

if TYPE_CHECKING:
    from mac.medium import SimMedium
    from mac.phy_sap import PhySap


class DcfState(Enum):
    IDLE = auto()
    WAIT_DIFS = auto()
    BACKOFF = auto()
    TX = auto()
    WAIT_ACK = auto()
    RX = auto()


class DcfStation:

    def __init__(
        self,
        address: bytes,
        medium: SimMedium,
        phy: PhySap,
        *,
        on_msdu_rx: Callable[[bytes, bytes, int], None] | None = None,
    ) -> None:
        self.address = address
        self.medium = medium
        self.phy = phy
        self.on_msdu_rx = on_msdu_rx  # (sa, payload, seq) when a data frame is accepted

        self.state = DcfState.IDLE
        self.cw = CW_MIN
        self.backoff_slots: int | None = None
        self.retry_count = 0
        self.tx_queue: deque[bytes] = deque()  # MPDU bytes
        self.current_mpdu: bytes | None = None
        self._ack_timeout_token = 0  # invalidate stale timeouts
        self._difs_token = 0
        self.stats = {
            "tx_attempts": 0,
            "tx_success": 0,
            "tx_fail": 0,
            "retries": 0,
            "acks_rx": 0,
            "data_rx": 0,
        }

    # ── stubs (implement in a subclass) ───────────────────────────────────────

    def on_msdu_enqueue(self, mpdu: bytes) -> None:
        self.tx_queue.append(mpdu)
        if self.state == DcfState.IDLE and self.current_mpdu is None:
            self._kick_tx()

    def _kick_tx(self) -> None:
        if self.current_mpdu is None:
            if not self.tx_queue:
                self.state = DcfState.IDLE
                return
            self.current_mpdu = self.tx_queue.popleft()
            self.retry_count = 0
            self.cw = CW_MIN
            self.backoff_slots = None
        if self.medium.is_idle():
            self.on_channel_idle_edge()
        else:
            self.state = DcfState.IDLE

            def _poll() -> None:
                if self.medium.is_idle():
                    self.on_channel_idle_edge()
                else:
                    self.medium.schedule(SLOT_US, _poll)

            self.medium.schedule(SLOT_US, _poll)

    def on_channel_idle_edge(self) -> None:
        if self.current_mpdu is None and not self.tx_queue:
            self.state = DcfState.IDLE
            return

        self.state = DcfState.WAIT_DIFS
        self._difs_token += 1
        token = self._difs_token

        def _wait_idle_then_retry() -> None:
            def _poll() -> None:
                if self.medium.is_idle():
                    self.on_channel_idle_edge()
                else:
                    self.medium.schedule(SLOT_US, _poll)
            self.medium.schedule(SLOT_US, _poll)

        def _schedule_slot() -> None:
            def _slot() -> None:
                if self.state != DcfState.BACKOFF:
                    return
                if not self.medium.is_idle():
                    self.state = DcfState.IDLE
                    _wait_idle_then_retry()
                    return
                self.on_slot()
                if self.state == DcfState.BACKOFF:
                    _schedule_slot()
            self.medium.schedule(SLOT_US, _slot)

        def _difs_done() -> None:
            if token != self._difs_token:
                return
            if not self.medium.is_idle():
                _wait_idle_then_retry()
                return
            if self.backoff_slots is None:
                self.backoff_slots = int(self.medium.rng.integers(0, self.cw + 1))
            self.state = DcfState.BACKOFF
            _schedule_slot()

        self.medium.schedule(DIFS_US, _difs_done)


    def on_slot(self) -> None:
        self.backoff_slots -= 1
        if self.backoff_slots == 0:
            self.start_tx()

    def start_tx(self) -> None:
        if self.current_mpdu is None:
            return
        self.state = DcfState.TX
        self.stats["tx_attempts"] += 1

        samples, airtime = encode_mpdu(self.phy, self.current_mpdu)
        frame = parse_mpdu(self.current_mpdu)
        dst = frame.addr1 if frame is not None else None

        self.medium.transmit(
            self.address,
            samples,
            airtime,
            self.current_mpdu,
            dst_addr=dst,
        )

        self.state = DcfState.WAIT_ACK
        self._ack_timeout_token += 1
        token = self._ack_timeout_token
        timeout = ACK_TIMEOUT_US + airtime

        def _timeout() -> None:
            if token != self._ack_timeout_token:
                return
            if self.state == DcfState.WAIT_ACK:
                self.on_ack_timeout()

        self.medium.schedule(timeout, _timeout)

    def on_ack_timeout(self) -> None:
        self.retry_count += 1
        self.stats["retries"] += 1
        if self.retry_count > SHORT_RETRY_LIMIT:
            self.stats["tx_fail"] += 1
            self.current_mpdu = None
            self.backoff_slots = None
            self.cw = CW_MIN
            self.state = DcfState.IDLE
            self._kick_tx()
            return
        self.cw = min(CW_MAX, self.cw * 2 + 1)
        self.backoff_slots = None  # redraw after next DIFS
        self.state = DcfState.IDLE
        self._kick_tx()

    def on_rx_mpdu(self, frame: MacFrame) -> None:
        if frame.is_ack:
            if frame.addr1 == self.address:
                self.on_rx_ack(frame)
            return

        if not frame.is_data:
            return
        if frame.addr1 != self.address:
            return

        self.stats["data_rx"] += 1
        sa = frame.addr2 or b"\x00" * 6
        if self.on_msdu_rx is not None:
            self.on_msdu_rx(sa, frame.payload, frame.seq)

        ack_mpdu = pack_ack(sa)

        def _send_ack() -> None:
            if not self.medium.is_idle():
                return
            samples, airtime = encode_mpdu(self.phy, ack_mpdu)
            self.medium.transmit(
                self.address,
                samples,
                airtime,
                ack_mpdu,
                dst_addr=sa,
            )

        self.medium.schedule(SIFS_US, _send_ack)

    def on_rx_ack(self, frame: MacFrame) -> None:
        if self.state != DcfState.WAIT_ACK:
            return
        if frame.addr1 != self.address:
            return

        self._ack_timeout_token += 1  # invalidate pending timeout
        self.stats["acks_rx"] += 1
        self.stats["tx_success"] += 1

        self.current_mpdu = None
        self.retry_count = 0
        self.cw = CW_MIN
        self.backoff_slots = None
        self.state = DcfState.IDLE
        self._kick_tx()
