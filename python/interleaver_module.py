import numpy as np


def interleave(inputVec, n_cbps, n_bpsc):
    interleavedOutput = np.zeros_like(inputVec)
    for k in range(n_cbps):
        i = int((n_cbps // 16) * (k % 16) + k // 16)
        s = max(n_bpsc // 2, 1)
        j = int(s * (i // s) + (i + n_cbps - (16 * i // n_cbps)) % s)
        interleavedOutput[j] = inputVec[k]
    return interleavedOutput


def deinterleave(inputVec, n_cbps, n_bpsc):
    output = np.zeros_like(inputVec)
    for k in range(n_cbps):
        i = int((n_cbps // 16) * (k % 16) + k // 16)
        s = max(n_bpsc // 2, 1)
        j = int(s * (i // s) + (i + n_cbps - (16 * i // n_cbps)) % s)
        output[k] = inputVec[j]
    return output
