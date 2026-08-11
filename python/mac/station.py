"""MacStation: MSDU API wired to framing, DCF, medium, and reorder."""

from __future__ import annotations

from collections import defaultdict, deque

from mac.dcf import DcfStation
from mac.frame import pack_data, parse_mpdu
from mac.medium import SimMedium
from mac.phy_sap import PhySap
from mac.reorder import PassthroughReorder, ReorderBuffer


def addr_from_int(n: int) -> bytes:
    """Map a small integer station id to a 6-byte MAC address."""
    return n.to_bytes(6, "big")


class MacStation:
    """
    Public API:

        station.send(dst, msdu_bytes)
        station.recv() -> list[bytes]   # drain delivered MSDUs
    """

    def __init__(
        self,
        address: bytes,
        medium: SimMedium,
        phy: PhySap | None = None,
        *,
        dcf_cls: type[DcfStation] = DcfStation,
        reorder_factory: type[ReorderBuffer] = PassthroughReorder,
    ) -> None:
        self.address = address
        self.medium = medium
        self.phy = phy if phy is not None else medium.phy
        self._next_seq = 0
        self._rx_msdus: deque[bytes] = deque()
        self._reorder: dict[bytes, ReorderBuffer] = defaultdict(reorder_factory)

        self.dcf: DcfStation = dcf_cls(
            address,
            medium,
            self.phy,
            on_msdu_rx=self._on_msdu_from_dcf,
        )
        medium.register(self)

    # ── MediumPeer ────────────────────────────────────────────────────────────

    def on_medium_rx(self, mpdu: bytes) -> None:
        frame = parse_mpdu(mpdu)
        if frame is None:
            return
        self.dcf.on_rx_mpdu(frame)

    # ── App API ───────────────────────────────────────────────────────────────

    def send(self, dst: bytes | int, msdu: bytes) -> None:
        if isinstance(dst, int):
            dst = addr_from_int(dst)
        seq = self._next_seq
        self._next_seq = (self._next_seq + 1) & 0xFFF
        mpdu = pack_data(dst, self.address, seq, msdu)
        self.dcf.on_msdu_enqueue(mpdu)

    def recv(self) -> list[bytes]:
        out = list(self._rx_msdus)
        self._rx_msdus.clear()
        return out

    def _on_msdu_from_dcf(self, sa: bytes, payload: bytes, seq: int) -> None:
        released = self._reorder[sa].insert(seq, payload)
        self._rx_msdus.extend(released)
