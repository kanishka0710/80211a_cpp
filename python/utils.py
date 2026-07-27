def fft_bin_from_subcarrier(k, nFFT):
    return k if k > 0 else nFFT + k

def are_close(a, b, epsilon=1e-6):
    return abs(a - b) < epsilon

def int_to_bits(n, numBits=None):
    """LSB-first bit list (bit 0 = LSB), matching 802.11a transmit order."""
    if numBits is None:
        return [int(bit) for bit in bin(n)[2:]][::-1]
    return [(n >> i) & 1 for i in range(numBits)]