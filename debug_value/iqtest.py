from os import read

from scipy import signal
import numpy as np
import matplotlib.pyplot as plt
import readhex
from typing import Any, cast


def lpf_100k(seq: np.ndarray) -> np.ndarray:
    fs = 1.024e6
    f_pass = 100e3
    f_stop = 250e3
    attenuation_db = 60.0

    normalized_width = (f_stop - f_pass) / (fs / 2.0)

    num_taps_raw, beta_raw = signal.kaiserord(
        ripple=attenuation_db,
        width=normalized_width,
    )

    num_taps = int(num_taps_raw)
    beta = float(beta_raw)
    window_spec: Any = ("kaiser", beta)

    b = signal.firwin(
        numtaps=num_taps,
        cutoff=175e3,
        window=window_spec,
        pass_zero=True,
        fs=fs,
        scale=True,
    )

    y = signal.lfilter(b, 1.0, seq)
    return cast(np.ndarray, y)


fs = 1.024e6
n = 4096
Ts = 1 / fs
carrier_freq = 200e3
timeseq = np.arange(n) * Ts
sampled_seq = readhex.readhex("adc_buffer_16384_dc_blocked.hex")
sampled_seq_mean = np.mean(sampled_seq)
# sampled_seq_blocked = sampled_seq - sampled_seq_mean
# i_seq = sampled_seq_blocked * np.cos(2 * np.pi * carrier_freq * timeseq)
# q_seq = sampled_seq_blocked * np.sin(2 * np.pi * carrier_freq * timeseq)
# i_seq = lpf_100k(i_seq)
# q_seq = lpf_100k(q_seq)
i_seq = readhex.readhex("Ibuffer_16384.hex")
q_seq = readhex.readhex("Qbuffer_16384.hex")

envelope_seq = (i_seq * i_seq) + (q_seq * q_seq)
envelope_seq = np.sqrt(envelope_seq)
envelope_seq = np.abs(np.fft.fft(envelope_seq))
plt.plot(range(len(i_seq)), np.abs(np.fft.fft(envelope_seq)))
plt.show()
