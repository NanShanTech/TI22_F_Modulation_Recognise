import readhex
import numpy as np
import matplotlib.pyplot as plt


def dc_blocker(seq: np.ndarray) -> np.ndarray:
    mean = np.mean(seq)
    return seq - mean


sample_seq = readhex.readhex("adc_buffer_16384_original.hex")
sample_seq_blocker = dc_blocker(sample_seq)
plt.plot(range(len(sample_seq_blocker)), np.abs(np.fft.fft(sample_seq_blocker)))
plt.show()
