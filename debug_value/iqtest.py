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


fs = 1.024e6
n = 4096
Ts = 1 / fs
carrier_freq = 200e3
timeseq = np.arange(n) * Ts
sampled_seq = readhex.readhex("adc_buffer_16384_3.hex")
sampled_seq_mean = np.mean(sampled_seq)
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
# envelope_seq = sampled_seq_blocked
corrlation_list = []
for i in range(1, 11):
    freq_test = i * 1e3
    test_sig = np.cos(2 * np.pi * freq_test * timeseq)
    env_corr = np.correlate(envelope_seq, test_sig)
    corr_peak = np.max(env_corr)
    corrlation_list.append(corr_peak)

corrlation_list = np.array(corrlation_list)
max_corr = np.max(corrlation_list)
max_corr_index = np.argmax(corrlation_list)
max_corr_index += 1
max_corr_index *= 1e3

freq_corr_list = []
for i in range(1, 11):
    freq_test = i * 1e3
    test_sig = np.cos(2 * np.pi * freq_test * timeseq)
    freq_corr = np.correlate(freq_seq, test_sig)
    corr_peak = np.max(freq_corr)
    freq_corr_list.append(corr_peak)
freq_corr_list = np.array(freq_corr_list)
max_freq_corr = np.max(freq_corr_list)
max_freq_corr_index = np.argmax(freq_corr_list)
max_freq_corr_index += 1
max_freq_corr_index *= 1e3

test_sig_cos = np.cos(2 * np.pi * freq_test * timeseq)
test_sig_sin = np.sin(2 * np.pi * freq_test * timeseq)
power_sin = test_sig_sin * envelope_seq
power_cos = test_sig_cos * envelope_seq
power_sin = np.sum(power_sin)
power_cos = np.sum(power_cos)
power = np.sqrt((power_sin * power_sin) + (power_cos * power_cos))
power *= 2 / n
test_sig_cos = np.cos(2 * np.pi * freq_test * timeseq)
test_sig_sin = np.sin(2 * np.pi * freq_test * timeseq)
freq_power_sin = test_sig_sin[:4095] * freq_seq
freq_power_cos = test_sig_cos[:4095] * freq_seq
freq_power_sin = np.sum(freq_power_sin)
freq_power_cos = np.sum(freq_power_cos)
freq_power = np.sqrt(
    (freq_power_sin * freq_power_sin) + (freq_power_cos * freq_power_cos)
)
freq_power *= 2 / n
print(max_corr_index)
print(power)
print(max_freq_corr_index)
print(freq_power)
plt.plot(
    np.arange(-1 * 512e3, 512e3, 250), np.abs(np.fft.fftshift(np.fft.fft(envelope_seq)))
)
plt.show()
