from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class ReorderBuffer:
    """
    Per-source reorder state. MacStation typically keeps one buffer per SA,
    or a dict[addr, ReorderBuffer].
    """

    window_size: int = 64
    expected: int = 0  # next seq to release (12-bit space, wraps at 4096)
    _buf: dict[int, bytes] = field(default_factory=dict)

    def insert(self, seq: int, payload: bytes) -> list[bytes]:
        """
        Insert a payload with MAC sequence number `seq`.

        Returns a list of payloads released in order (possibly empty).
        """
        seq = seq & 0xFFF
        released = []

        distance = (seq - self.expected) & 0xFFF  # wrap-aware forward distance

        if distance >= self.window_size:
            return released

        if seq in self._buf:
            return released

        if seq == self.expected:
            released.append(payload)
            self.expected = (self.expected + 1) & 0xFFF
            while self.expected in self._buf:
                released.append(self._buf.pop(self.expected))
                self.expected = (self.expected + 1) & 0xFFF
        else:
            self._buf[seq] = payload

        return released


class PassthroughReorder(ReorderBuffer):

    def insert(self, seq: int, payload: bytes) -> list[bytes]:
        self.expected = (seq + 1) & 0xFFF
        return [payload]
