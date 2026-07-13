import numpy as np

from config import CodingRates


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

    state = 0
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



