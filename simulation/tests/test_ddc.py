import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import modulation
import ddc
import hardware_sim as sim

FS_ANA = 262.144e6
N_ANA = 524288
FS = 2.048e6
N_PTS = 4096
CARRIER_FREQ = 10e6  # 载波频率 10 MHz
CARRIER_AMP = 1.0  # 载波幅度 1 V
MOD_FREQ = 3e3  # 调制频率 3 kHz
MOD_AMP = 0.5  # 调制幅度 0.1 (即 AM 调制指数 / FM 频偏系数)

seq = modulation.fm_modulation(
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
print(lo_best)
