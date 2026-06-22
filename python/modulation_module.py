import math
from typing import List

from config import ModulationTypes

_NORMALIZATION_CONSTANT = {
    ModulationTypes.BPSK:  1.0,
    ModulationTypes.QPSK:  1.0 / math.sqrt(2.0),
    ModulationTypes.QAM16: 1.0 / math.sqrt(10.0),
    ModulationTypes.QAM64: 1.0 / math.sqrt(42.0),
}

_BPSK_MAP = {
    0: (-1.0 + 0j),
    1: ( 1.0 + 0j),
}

# QPSK — b0->I, b1->Q; 0->-1, 1->+1; caller multiplies by 1/sqrt(2)
_QPSK_MAP = {
    0b00: (-1.0 - 1.0j),
    0b01: (-1.0 + 1.0j),
    0b10: ( 1.0 - 1.0j),
    0b11: ( 1.0 + 1.0j),
}

# 16-QAM — b0b1->I, b2b3->Q; 2-bit Gray: 00->-3, 01->-1, 10->+3, 11->+1
# caller multiplies by 1/sqrt(10)
_QAM16_MAP = {
    0b0000: (-3.0 - 3.0j), 0b0001: (-3.0 - 1.0j),
    0b0010: (-3.0 + 3.0j), 0b0011: (-3.0 + 1.0j),
    0b0100: (-1.0 - 3.0j), 0b0101: (-1.0 - 1.0j),
    0b0110: (-1.0 + 3.0j), 0b0111: (-1.0 + 1.0j),
    0b1000: ( 3.0 - 3.0j), 0b1001: ( 3.0 - 1.0j),
    0b1010: ( 3.0 + 3.0j), 0b1011: ( 3.0 + 1.0j),
    0b1100: ( 1.0 - 3.0j), 0b1101: ( 1.0 - 1.0j),
    0b1110: ( 1.0 + 3.0j), 0b1111: ( 1.0 + 1.0j),
}

# 64-QAM — b0b1b2->I, b3b4b5->Q; 3-bit Gray: 000->-7, 001->-5, 010->-1,
# 011->-3, 100->+7, 101->+5, 110->+1, 111->+3; caller multiplies by 1/sqrt(42)
_QAM64_MAP = {
    0b000000: (-7.0 - 7.0j), 0b000001: (-7.0 - 5.0j),
    0b000010: (-7.0 - 1.0j), 0b000011: (-7.0 - 3.0j),
    0b000100: (-7.0 + 7.0j), 0b000101: (-7.0 + 5.0j),
    0b000110: (-7.0 + 1.0j), 0b000111: (-7.0 + 3.0j),
    0b001000: (-5.0 - 7.0j), 0b001001: (-5.0 - 5.0j),
    0b001010: (-5.0 - 1.0j), 0b001011: (-5.0 - 3.0j),
    0b001100: (-5.0 + 7.0j), 0b001101: (-5.0 + 5.0j),
    0b001110: (-5.0 + 1.0j), 0b001111: (-5.0 + 3.0j),
    0b010000: (-1.0 - 7.0j), 0b010001: (-1.0 - 5.0j),
    0b010010: (-1.0 - 1.0j), 0b010011: (-1.0 - 3.0j),
    0b010100: (-1.0 + 7.0j), 0b010101: (-1.0 + 5.0j),
    0b010110: (-1.0 + 1.0j), 0b010111: (-1.0 + 3.0j),
    0b011000: (-3.0 - 7.0j), 0b011001: (-3.0 - 5.0j),
    0b011010: (-3.0 - 1.0j), 0b011011: (-3.0 - 3.0j),
    0b011100: (-3.0 + 7.0j), 0b011101: (-3.0 + 5.0j),
    0b011110: (-3.0 + 1.0j), 0b011111: (-3.0 + 3.0j),
    0b100000: ( 7.0 - 7.0j), 0b100001: ( 7.0 - 5.0j),
    0b100010: ( 7.0 - 1.0j), 0b100011: ( 7.0 - 3.0j),
    0b100100: ( 7.0 + 7.0j), 0b100101: ( 7.0 + 5.0j),
    0b100110: ( 7.0 + 1.0j), 0b100111: ( 7.0 + 3.0j),
    0b101000: ( 5.0 - 7.0j), 0b101001: ( 5.0 - 5.0j),
    0b101010: ( 5.0 - 1.0j), 0b101011: ( 5.0 - 3.0j),
    0b101100: ( 5.0 + 7.0j), 0b101101: ( 5.0 + 5.0j),
    0b101110: ( 5.0 + 1.0j), 0b101111: ( 5.0 + 3.0j),
    0b110000: ( 1.0 - 7.0j), 0b110001: ( 1.0 - 5.0j),
    0b110010: ( 1.0 - 1.0j), 0b110011: ( 1.0 - 3.0j),
    0b110100: ( 1.0 + 7.0j), 0b110101: ( 1.0 + 5.0j),
    0b110110: ( 1.0 + 1.0j), 0b110111: ( 1.0 + 3.0j),
    0b111000: ( 3.0 - 7.0j), 0b111001: ( 3.0 - 5.0j),
    0b111010: ( 3.0 - 1.0j), 0b111011: ( 3.0 - 3.0j),
    0b111100: ( 3.0 + 7.0j), 0b111101: ( 3.0 + 5.0j),
    0b111110: ( 3.0 + 1.0j), 0b111111: ( 3.0 + 3.0j),
}

_CONSTELLATION_MAP = {
    ModulationTypes.BPSK:  _BPSK_MAP,
    ModulationTypes.QPSK:  _QPSK_MAP,
    ModulationTypes.QAM16: _QAM16_MAP,
    ModulationTypes.QAM64: _QAM64_MAP,
}


def _unpack_msb(key: int, n_bpsc: int) -> List[int]:
    return [(key >> (n_bpsc - 1 - b)) & 1 for b in range(n_bpsc)]


def _pack_msb(bits: List[int], modulation: str) -> int:
    if modulation == ModulationTypes.BPSK:
        return bits[0]
    if modulation == ModulationTypes.QPSK:
        return (bits[0] << 1) | bits[1]
    if modulation == ModulationTypes.QAM16:
        return (bits[0] << 3) | (bits[1] << 2) | (bits[2] << 1) | bits[3]
    if modulation == ModulationTypes.QAM64:
        return (bits[0] << 5) | (bits[1] << 4) | (bits[2] << 3) | (bits[3] << 2) | (bits[4] << 1) | bits[5]
    return 0



def map_bits_to_constellation(bits: List[int], modulation: str, n_bpsc: int) -> List[complex]:
    k_mod = _NORMALIZATION_CONSTANT[modulation]
    cmap = _CONSTELLATION_MAP[modulation]
    result = []
    for i in range(0, len(bits) - n_bpsc + 1, n_bpsc):
        key = _pack_msb(bits[i:i + n_bpsc], modulation)
        point = cmap[key]
        result.append(point if modulation == ModulationTypes.BPSK else point * k_mod)
    return result
    

def map_constellation_to_bits(symbols: List[complex], modulation: str, n_bpsc: int) -> List[int]:
    k_mod = _NORMALIZATION_CONSTANT[modulation]
    cmap = _CONSTELLATION_MAP[modulation]
    bits = []
    for symbol in symbols:
        if modulation == ModulationTypes.BPSK:
            bits.append(1 if symbol.real > 0 else 0)
        else:
            normalized = symbol / k_mod
            closest_key = min(cmap, key=lambda k: abs(normalized - cmap[k]))
            bits.extend(_unpack_msb(closest_key, n_bpsc))
    return bits
