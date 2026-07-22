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


def bpf_100k_300k(seq: np.ndarray) -> np.ndarray:
    fs = 1.024e6

    f_stop_low = 10e3
    f_pass_low = 100e3

    f_pass_high = 300e3
    f_stop_high = 350e3

    attenuation_db = 60.0

    transition_low = f_pass_low - f_stop_low
    transition_high = f_stop_high - f_pass_high

    # 阶数由更窄的过渡带决定，此处为右侧 50 kHz。
    transition_width = min(transition_low, transition_high)
    normalized_width = transition_width / (fs / 2.0)

    num_taps_raw, beta_raw = signal.kaiserord(
        ripple=attenuation_db,
        width=normalized_width,
    )

    num_taps = int(num_taps_raw)
    if num_taps % 2 == 0:
        num_taps += 1

    beta = float(beta_raw)
    window_spec: Any = ("kaiser", beta)

    # 左、右 cutoff 均取对应过渡带中心。
    cutoff_low = (f_stop_low + f_pass_low) / 2.0
    cutoff_high = (f_pass_high + f_stop_high) / 2.0

    b = signal.firwin(
        numtaps=num_taps,
        cutoff=[cutoff_low, cutoff_high],
        window=window_spec,
        pass_zero=False,
        fs=fs,
        scale=True,
    )

    y = signal.lfilter(b, 1.0, seq)
    return cast(np.ndarray, y)


def design_lpf_decimate_128k() -> np.ndarray:
    fs_in = 1.024e6
    decimation = 8

    f_pass = 12e3
    f_stop = 40e3
    attenuation_db = 60.0

    normalized_width = (f_stop - f_pass) / (fs_in / 2.0)

    num_taps_raw, beta_raw = signal.kaiserord(
        ripple=attenuation_db,
        width=normalized_width,
    )

    beta = float(beta_raw)

    num_taps = int(
        np.ceil((int(num_taps_raw) - 1) / (2 * decimation)) * (2 * decimation) + 1
    )

    cutoff = 0.5 * (f_pass + f_stop)
    window_spec: Any = ("kaiser", beta)

    coeffs = signal.firwin(
        numtaps=num_taps,
        cutoff=cutoff,
        window=window_spec,
        pass_zero=True,
        fs=fs_in,
        scale=True,
    )

    return cast(np.ndarray, coeffs)


LPF_DECIMATE_128K_COEFFS = design_lpf_decimate_128k()


def lpf_decimate_128k(seq: np.ndarray) -> np.ndarray:
    filtered = signal.lfilter(
        LPF_DECIMATE_128K_COEFFS,
        1.0,
        seq,
    )

    decimated = filtered[::8]

    return cast(np.ndarray, decimated)


fs = 1.024e6
n = 4096
fs_down = 128e3
n_down = 512
Ts = 1 / fs
carrier_freq = 200e3
timeseq = np.arange(n) * Ts
sampled_seq = readhex.readhex("adc_buffer_16384_3.hex")
sampled_seq_mean = np.mean(sampled_seq)
sampled_seq = sampled_seq - sampled_seq_mean
sampled_seq /= np.abs(sampled_seq_mean)
sampled_seq_blocked = bpf_100k_300k(sampled_seq)
i_seq = sampled_seq_blocked * np.cos(2 * np.pi * carrier_freq * timeseq)
q_seq = sampled_seq_blocked * np.sin(2 * np.pi * carrier_freq * timeseq)
i_seq = lpf_100k(i_seq)
q_seq = lpf_100k(q_seq)
# i_seq = readhex.readhex("Ibuffer_16384.hex")
# q_seq = readhex.readhex("Qbuffer_16384.hex")

envelope_seq = (i_seq * i_seq) + (q_seq * q_seq)
envelope_seq = np.sqrt(envelope_seq)
envelope_seq_mean = np.mean(envelope_seq)
envelope_seq -= envelope_seq_mean  # 均值归一化
envelope_seq /= envelope_seq_mean
freq_seq = []
for i in range(1, len(sampled_seq)):
    cross = (i_seq[i - 1] * q_seq[i]) - (i_seq[i] * q_seq[i - 1])
    dot = (i_seq[i - 1] * i_seq[i]) + (q_seq[i - 1] * q_seq[i]) + 1e-6
    delta_phi = cross / dot
    f = delta_phi / (2 * np.pi * Ts)
    freq_seq.append(f)
freq_seq = np.array(freq_seq)
freq_seq_mean = np.mean(freq_seq)
freq_seq -= freq_seq_mean
envelope_seq_down = lpf_decimate_128k(envelope_seq)
freq_seq_down = lpf_decimate_128k(freq_seq)
fs_show = fs
n_show = n
plt.plot(
    np.arange(-1 * fs_show / 2, fs_show / 2, fs_show / n_show),
    np.abs(np.fft.fftshift(np.fft.fft(envelope_seq))),
)
plt.show()
