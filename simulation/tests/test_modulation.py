import numpy as np
import matplotlib.pyplot as plt
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import modulation

fs_hz = 1.024 * 1e6  # 采样率1Mhz
n_pts = 2048
carrier_freq = 10e6  # 载波频率10Mhz
carrier_amp = 1  # 1V
modulation_freq = 2e3  # 2khz
modulation_amp = 0.1  # 调制波幅度100mv

am_seq = modulation.am_modulation(
    fs_hz=fs_hz,
    n_pts=n_pts,
    carrier_freq=carrier_freq,
    carrier_amp=carrier_amp,
    modulation_freq=modulation_freq,
    modulation_amp=modulation_amp,
)
Ts = 1 / fs_hz
time_seq = np.arange(0, 1, Ts)
time_seq = time_seq[0:n_pts]
freq_axis = np.arange(-1 * fs_hz / 2, fs_hz / 2, fs_hz / n_pts)
am_seq_fft = np.fft.fftshift(np.fft.fft(am_seq, n_pts))
am_seq_fft_abs = np.abs(am_seq_fft)
plt.plot(time_seq, am_seq)
plt.title("am timedomain seq")
plt.show()
plt.plot(freq_axis, am_seq_fft_abs)
plt.title("am amp spectrum")
plt.show()

fm_seq = modulation.fm_modulation(
    fs_hz=fs_hz,
    n_pts=n_pts,
    carrier_freq=carrier_freq,
    carrier_amp=carrier_amp,
    modulation_freq=modulation_freq,
    modulation_amp=modulation_amp,
)
fm_seq_fft = np.fft.fftshift(np.fft.fft(fm_seq, n_pts))
fm_seq_fft_abs = np.abs(fm_seq_fft)
plt.plot(time_seq, fm_seq)
plt.title("fm timedomain seq")
plt.show()
plt.plot(freq_axis, fm_seq_fft_abs)
plt.title("fm amp spectrum")
plt.show()
