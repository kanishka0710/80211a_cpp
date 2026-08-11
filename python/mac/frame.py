from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

import numpy as np

# Frame Control type / subtype (subset of 802.11)
FC_TYPE_DATA = 0b10
FC_TYPE_CTRL = 0b01
FC_SUBTYPE_DATA = 0b0000
FC_SUBTYPE_ACK = 0b1101

ADDR_LEN = 6
FC_LEN = 2
DUR_LEN = 2
SEQ_LEN = 2
FCS_LEN = 4

DATA_HDR_LEN = FC_LEN + DUR_LEN + ADDR_LEN + ADDR_LEN + SEQ_LEN  # 18
ACK_HDR_LEN = FC_LEN + DUR_LEN + ADDR_LEN  # 10


def _make_frame_control(type_: int, subtype: int) -> int:
    """Pack Protocol Version=0, Type, Subtype into a 16-bit FC (ToDS/FromDS=0)."""
    return ((subtype & 0xF) << 4) | ((type_ & 0x3) << 2)


def _parse_frame_control(fc: int) -> tuple[int, int]:
    type_ = (fc >> 2) & 0x3
    subtype = (fc >> 4) & 0xF
    return type_, subtype


def bytes_to_bits(data: bytes) -> np.ndarray:
    """MSB-first within each byte → flat 0/1 int array (PHY PSDU)."""
    if not data:
        return np.zeros(0, dtype=int)
    arr = np.frombuffer(data, dtype=np.uint8)
    # bit 7 .. bit 0 of each octet
    bits = np.unpackbits(arr, bitorder="big")
    return bits.astype(int)


def bits_to_bytes(bits: np.ndarray) -> bytes:
    """Inverse of bytes_to_bits; truncates to a multiple of 8 bits."""
    bits = np.asarray(bits, dtype=np.uint8).ravel()
    n = (len(bits) // 8) * 8
    if n == 0:
        return b""
    packed = np.packbits(bits[:n], bitorder="big")
    return packed.tobytes()


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _pack_addr(addr: bytes | int) -> bytes:
    if isinstance(addr, int):
        return addr.to_bytes(ADDR_LEN, "big")
    if len(addr) != ADDR_LEN:
        raise ValueError(f"address must be {ADDR_LEN} bytes, got {len(addr)}")
    return bytes(addr)


@dataclass
class MacFrame:
    type_: int
    subtype: int
    duration_id: int
    addr1: bytes  # RA
    addr2: bytes | None  # TA (None for ACK)
    seq: int  # 12-bit sequence number
    frag: int  # 4-bit fragment number
    payload: bytes
    fcs_ok: bool = True

    @property
    def is_data(self) -> bool:
        return self.type_ == FC_TYPE_DATA and self.subtype == FC_SUBTYPE_DATA

    @property
    def is_ack(self) -> bool:
        return self.type_ == FC_TYPE_CTRL and self.subtype == FC_SUBTYPE_ACK

    @property
    def seq_control(self) -> int:
        return ((self.seq & 0xFFF) << 4) | (self.frag & 0xF)


def pack_data(
    addr1: bytes | int,
    addr2: bytes | int,
    seq: int,
    payload: bytes,
    *,
    duration_id: int = 0,
    frag: int = 0,
) -> bytes:
    """Build a data MPDU (header + payload + FCS)."""
    fc = _make_frame_control(FC_TYPE_DATA, FC_SUBTYPE_DATA)
    seq_ctrl = ((seq & 0xFFF) << 4) | (frag & 0xF)
    body = (
        struct.pack("<HH", fc, duration_id & 0xFFFF)
        + _pack_addr(addr1)
        + _pack_addr(addr2)
        + struct.pack("<H", seq_ctrl)
        + payload
    )
    fcs = _crc32(body)
    return body + struct.pack("<I", fcs)


def pack_ack(addr1: bytes | int, *, duration_id: int = 0) -> bytes:
    """Build an ACK MPDU (RA = addr1 of the data frame being acknowledged)."""
    fc = _make_frame_control(FC_TYPE_CTRL, FC_SUBTYPE_ACK)
    body = struct.pack("<HH", fc, duration_id & 0xFFFF) + _pack_addr(addr1)
    fcs = _crc32(body)
    return body + struct.pack("<I", fcs)


def parse_mpdu(raw: bytes) -> MacFrame | None:
    """
    Parse an MPDU. Returns None if too short or FCS fails.
    """
    if len(raw) < ACK_HDR_LEN + FCS_LEN:
        return None

    body, fcs_bytes = raw[:-FCS_LEN], raw[-FCS_LEN:]
    (fcs_rx,) = struct.unpack("<I", fcs_bytes)
    if _crc32(body) != fcs_rx:
        return None

    fc, duration_id = struct.unpack_from("<HH", body, 0)
    type_, subtype = _parse_frame_control(fc)
    addr1 = body[4:10]

    if type_ == FC_TYPE_CTRL and subtype == FC_SUBTYPE_ACK:
        if len(body) != ACK_HDR_LEN:
            return None
        return MacFrame(
            type_=type_,
            subtype=subtype,
            duration_id=duration_id,
            addr1=addr1,
            addr2=None,
            seq=0,
            frag=0,
            payload=b"",
            fcs_ok=True,
        )

    if type_ == FC_TYPE_DATA and subtype == FC_SUBTYPE_DATA:
        if len(body) < DATA_HDR_LEN:
            return None
        addr2 = body[10:16]
        (seq_ctrl,) = struct.unpack_from("<H", body, 16)
        payload = body[DATA_HDR_LEN:]
        return MacFrame(
            type_=type_,
            subtype=subtype,
            duration_id=duration_id,
            addr1=addr1,
            addr2=addr2,
            seq=(seq_ctrl >> 4) & 0xFFF,
            frag=seq_ctrl & 0xF,
            payload=payload,
            fcs_ok=True,
        )

    return None
