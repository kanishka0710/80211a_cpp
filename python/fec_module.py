from math import ceil

import numpy as np

from config import CodingRates

SERVICE_BITS = 16   # 17.3.5.1: 7 scrambler-sync zeros + 9 reserved zeros
DATA_TAIL_BITS = 6  # 17.3.5.2


def LFSRStep(regs):
    feedback = regs[3] ^ regs[6]
    prbs_bit = feedback
    for i in range(len(regs) - 1, 0, -1):
        regs[i] = regs[i - 1]
    regs[0] = feedback
    return prbs_bit


def puncture(scrambledBits, rate):
    if rate == CodingRates.R12:
        return scrambledBits

    if rate == CodingRates.R34:
        # Pattern: A [1, 1, 0], B [1, 0, 1]
        outputBits = []
        for i in range(0, len(scrambledBits), 6):
            outputBits.append(scrambledBits[i])
            outputBits.append(scrambledBits[i + 1])
            outputBits.append(scrambledBits[i + 2])
            outputBits.append(scrambledBits[i + 5])
        return outputBits

    if rate == CodingRates.R23:
        # Pattern: A [1, 1], B [1, 0] — CodingRate R23
        outputBits = []
        for i in range(0, len(scrambledBits), 4):
            outputBits.append(scrambledBits[i])
            outputBits.append(scrambledBits[i + 1])
            outputBits.append(scrambledBits[i + 2])
        return outputBits


def convolutional_encoder_mother(scrambledBits):
    regs = [0, 0, 0, 0, 0, 0, 0]
    outputBits = []
    for bit in scrambledBits:
        for j in range(6):
            regs[j] = regs[j + 1]
        regs[6] = bit
        A = regs[0] ^ regs[1] ^ regs[3] ^ regs[4] ^ regs[6]
        B = regs[0] ^ regs[3] ^ regs[4] ^ regs[5] ^ regs[6]
        outputBits.append(A)
        outputBits.append(B)
    return outputBits


def append_convolutional_tail(scrambledBits):
    TAIL_BITS = 6
    return np.append(scrambledBits, np.zeros(TAIL_BITS, dtype=scrambledBits.dtype))


def _input_period_for_puncture(rate):
    """Mother-code input length must be a multiple of this for the puncture pattern."""
    if rate == CodingRates.R34:
        return 3
    if rate == CodingRates.R23:
        return 2
    return 1


def scramble(bits, seed7bit):
    register = np.zeros(7, dtype=np.int64)
    for i in range(7):
        register[i] = int((seed7bit >> i) & 1)

    scrambledBits = []
    for bit in bits:
        scrambledBits.append(int(LFSRStep(register)) ^ int(bit))

    return np.array(scrambledBits, dtype=int)


def perform_FEC(dataBits, codingRate, scramblerSeed7bit):
    scrambledBits = scramble(dataBits, scramblerSeed7bit)
    # Pad before the 6 tail bits so puncture groups are complete (802.11a-style).
    period = _input_period_for_puncture(codingRate)
    pad = (-(len(scrambledBits) + 6)) % period
    if pad:
        scrambledBits = np.append(scrambledBits, np.zeros(pad, dtype=int))
    tailedBits = append_convolutional_tail(scrambledBits)
    mother_bits = convolutional_encoder_mother(tailedBits)
    return puncture(mother_bits, codingRate)


def compute_ndbps(n_cbps, codingRate):
    """N_DBPS = N_CBPS * R -- data bits carried per OFDM symbol (Table 78)."""
    if codingRate == CodingRates.R12:
        return n_cbps // 2
    if codingRate == CodingRates.R23:
        return n_cbps * 2 // 3
    if codingRate == CodingRates.R34:
        return n_cbps * 3 // 4
    raise ValueError(f"Unknown coding rate: {codingRate}")


def compute_data_field_sizing(psduLenBits, nDbps):
    """N_SYM / N_PAD per 17.3.5.4, Eq. (11)-(13), for a SERVICE(16) + PSDU +
    TAIL(6) message of the given PSDU length."""
    n_msg_tail = SERVICE_BITS + psduLenBits + DATA_TAIL_BITS
    n_sym = ceil(n_msg_tail / nDbps)
    n_data = n_sym * nDbps
    n_pad = n_data - n_msg_tail
    return n_sym, n_pad


def perform_FEC_data_field(psduBits, codingRate, scramblerSeed7bit, nDbps):
    """Builds and encodes the PLCP DATA field per 17.3.5.

    Bit order is SERVICE(16 zeros) + PSDU + TAIL(6 zeros) + PAD(zeros), all
    scrambled together as one block (17.3.5.4); the 6 TAIL bits are then
    overwritten with unscrambled zeros (17.3.5.2) before rate-1/2
    convolutional encoding and puncturing to `codingRate`.
    """
    psduBits = np.asarray(psduBits, dtype=int)
    _, n_pad = compute_data_field_sizing(len(psduBits), nDbps)

    unscrambled = np.concatenate([
        np.zeros(SERVICE_BITS, dtype=int),
        psduBits,
        np.zeros(DATA_TAIL_BITS + n_pad, dtype=int),
    ])

    scrambledBits = scramble(unscrambled, scramblerSeed7bit)

    tail_start = SERVICE_BITS + len(psduBits)
    scrambledBits[tail_start:tail_start + DATA_TAIL_BITS] = 0

    mother_bits = convolutional_encoder_mother(scrambledBits)
    return puncture(mother_bits, codingRate)


def depuncture(bits, codingRate):
    if codingRate == CodingRates.R12:
        return bits, np.ones(len(bits), dtype=int)
    
    if codingRate == CodingRates.R34:
        outputBits = []
        bitsMask = []
        for i in range(0, len(bits), 4):
            outputBits.append(bits[i])
            bitsMask.append(1)
            outputBits.append(bits[i+1])
            bitsMask.append(1)
            outputBits.append(bits[i+2])
            bitsMask.append(1)
            outputBits.append(0)
            bitsMask.append(0)
            outputBits.append(0)
            bitsMask.append(0)
            outputBits.append(bits[i+3])
            bitsMask.append(1)
        return outputBits, bitsMask
    if codingRate == CodingRates.R23:
        outputBits = []
        bitsMask = []
        for i in range(0, len(bits), 3):
            outputBits.append(bits[i])
            bitsMask.append(1)
            outputBits.append(bits[i+1])
            bitsMask.append(1)
            outputBits.append(bits[i+2])
            bitsMask.append(1)
            outputBits.append(0)
            bitsMask.append(0)
        return outputBits, bitsMask

def precompute_trellis():
    next_states = np.zeros((64, 2), dtype=int)
    trellisA = np.zeros((64, 2), dtype=int)
    trellisB = np.zeros((64, 2), dtype=int)

    for s in range(64):
        for i in range(2):
            next_states[s, i] = (s >> 1) | (i << 5)
            trellisA[s, i] = (s&1) ^ ((s>>1)&1) ^ ((s>>3)&1) ^ ((s>>4)&1) ^ i
            trellisB[s, i] = (s&1) ^ ((s>>3)&1) ^ ((s>>4)&1) ^ ((s>>5)&1) ^ i

    return next_states, trellisA, trellisB

def viterbi_decode(bits, bitsMask):
    next_states, trellisA, trellisB = precompute_trellis()
    num_states = 64
    T = len(bits) // 2
    pathMetrics = np.full((num_states), np.inf)
    pathHistory = np.zeros((T, num_states), dtype=int)
    pathMetrics[0] = 0

    for t in range(T):
        rx_A = bits[2*t]
        rx_B = bits[2*t+1]

        newMetric = np.full((num_states), np.inf)
        for s in range(num_states):
            if pathMetrics[s] == np.inf:
                continue
            for u in range(2):
                hamming_distance = (rx_A ^ trellisA[s, u]) * bitsMask[2*t] + (rx_B ^ trellisB[s, u]) * bitsMask[2*t+1]
                ns = next_states[s, u]
                candidate = pathMetrics[s] + hamming_distance
                if candidate < newMetric[ns]:
                    newMetric[ns] = candidate
                    pathHistory[t, ns] = s
        pathMetrics = newMetric

    state = int(np.argmin(pathMetrics))
    decoded_bits = np.zeros(T, dtype=int)
    for t in range(T - 1, -1, -1):
        prevState = pathHistory[t, state]
        decoded_bits[t] = (state >> 5) & 1
        state = prevState

    return decoded_bits


def perform_FEC_RX(bits, codingRate, scramblerSeed7bit):
    depunctured, bitsMask = depuncture(bits, codingRate)
    decoded = viterbi_decode(depunctured, bitsMask)
    # Drop the 6 zero tail bits appended before convolutional encoding.
    return scramble(decoded[:-6], scramblerSeed7bit)


def perform_FEC_data_field_RX(bits, codingRate, scramblerSeed7bit):
    """Inverse of perform_FEC_data_field.

    Returns PSDU + TAIL + PAD bits (i.e. everything after the 16-bit SERVICE
    field); the caller trims to the PSDU length carried in the SIGNAL field.
    """
    depunctured, bitsMask = depuncture(bits, codingRate)
    decoded = viterbi_decode(depunctured, bitsMask)
    descrambled = scramble(decoded, scramblerSeed7bit)
    return descrambled[SERVICE_BITS:]



