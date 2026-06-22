def fft_bin_from_subcarrier(k, nFFT):
    return k if k > 0 else nFFT + k

def are_close(a, b, epsilon=1e-6):
    return abs(a - b) < epsilon