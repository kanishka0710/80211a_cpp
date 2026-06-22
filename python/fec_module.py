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


def scramble(bits, seed7bit):
    register = np.zeros(7, dtype=np.int64)
    for i in range(7):
        register[i] = int((seed7bit >> i) & 1)

    scrambledBits = []
    for bit in bits:
        scrambledBits.append(LFSRStep(register) ^ bit)

    return np.array(scrambledBits)


def perform_FEC(dataBits, codingRate, scramblerSeed7bit):
    scrambledBits = scramble(dataBits, scramblerSeed7bit)
    tailedBits = append_convolutional_tail(scrambledBits)
    mother_bits = convolutional_encoder_mother(tailedBits)
    return puncture(mother_bits, codingRate)


def depuncture(bits, codingRate):
    if codingRate == CodingRates.R12:
        return bits
    
    if codingRate == CodingRates.R34:
        outputBits = []
        for i in range(0, len(bits), 4):
            outputBits.append(bits[i])
            outputBits.append(bits[i+1])
            outputBits.append(bits[i+2])
            outputBits.append(0)
            outputBits.append(0)
            outputBits.append(bits[i+3])
        return outputBits
    if codingRate == CodingRates.R23:
        outputBits = []
        for i in range(0, len(bits), 3):
            outputBits.append(bits[i])
            outputBits.append(bits[i+1])
            outputBits.append(bits[i+2])
            outputBits.append(0)
        return outputBits
    return bits

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

def viterbi_decode(bits):
    next_states, trellisA, trellisB = precompute_trellis()
    num_states = 64
    T = len(bits) // 2
    pathMetrics = np.full((num_states), np.inf)
    pathHistory = np.zeros((T, num_states))
    pathMetrics[0] = 0

    for t in range(T):
        rx_A = bits[2*t]
        rx_B = bits[2*t+1]

        newMetric = np.full((num_states), np.inf)
        for s in range(num_states):
            if pathMetrics[s] == np.inf:
                continue
            for u in range(2):
                hamming_distance = (rx_A ^ trellisA[s, u]) + (rx_B ^ trellisB[s, u])
                ns = next_states[s, u]
                candidate = pathMetrics[s] + hamming_distance
                if candidate < newMetric[ns]:
                    newMetric[ns] = candidate
                    pathHistory[t, ns] = s
        pathMetrics = newMetric

    state = 0
    decoded_bits = np.zeros((T))
    for t in range(T-1, -1, -1):
        prevState = pathHistory[t, state]
        decoded_bits[t] = (state >> 5) & 1
        state = prevState

    return decoded_bits


def perform_FEC_RX(bits, codingRate, scramblerSeed7bit):
    depunctured = depuncture(bits, codingRate)
    decoded = viterbi_decode(depunctured)
    return scramble(decoded, scramblerSeed7bit)



