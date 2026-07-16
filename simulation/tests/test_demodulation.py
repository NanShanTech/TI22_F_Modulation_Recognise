import matplotlib.pyplot as plt
import numpy as np
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import modulation
import ddc
import hardware_sim as sim
import demodulation

FS_ANA = 262.144e6
N_ANA = 1048576
FS = 1.024e6
N_PTS = 4096
CARRIER_FREQ = 10e6  # 载波频率 10 MHz
CARRIER_AMP = 1.0  # 载波幅度 1 V
MOD_FREQ = 1e3  # 调制频率 3 kHz
MOD_AMP = 0.5  # 调制幅度 0.1 (即 AM 调制指数 / FM 频偏系数)
ENV_CV_GATE = 0.08
FREQ_CV_GATE = 0.0177

seq = modulation.am_modulation(
    fs_hz=FS_ANA,
    n_pts=N_ANA,
    carrier_freq=CARRIER_FREQ,
    carrier_amp=CARRIER_AMP,
    modulation_freq=MOD_FREQ,
    modulation_amp=MOD_AMP,
)

seq_gained1 = sim.gain(seq, 0.1)  # 模拟调制波通过增益后再放大的过程
seq_gained2 = sim.gain(seq, 1)

lo_best = ddc.find_best_lo(
    original_seq=seq_gained2,
    lo_start=9.8e6,
    lo_end=29.8e6,
    lo_step=0.5e6,
    freq_start=10e3,
    freq_end=300e3,
    fs_hz=FS,
    n_pts=N_PTS,
)

sampled_freq = ddc.mix_freq(lo_best, seq_gained2, N_PTS)
iq_samples = demodulation.get_iq(seq=sampled_freq, fc=200e3, fs_hz=FS, n_pts=N_PTS)
envelope = demodulation.get_envelope(iq_samples)
delta_f = demodulation.get_delta_f(iq_samples, FS)
modulation_method = demodulation.determine_modulation_method(
    envelope_seq=envelope,
    env_cv_gate=ENV_CV_GATE,
    freq_seq=delta_f,
    freq_cv_gate=FREQ_CV_GATE,
)
if modulation_method == 1:
    print("am modulation")
elif modulation_method == 2:
    print("fm modulation")
else:
    print("cw, no modulation")

demodulation_data = demodulation.demodulation(
    envelope_seq=envelope,
    delta_f_seq=delta_f,
    fs_hz=FS,
    modulation_type=modulation_method,
)
plt.plot(range(N_PTS), np.abs(np.fft.fft(iq_samples.i_seq)))
plt.show()
print("demodulation freq: %f", demodulation_data.freq)
print("demodulation amplitude %f", demodulation_data.amplitude)
print("modulation rate %f", demodulation_data.m)
