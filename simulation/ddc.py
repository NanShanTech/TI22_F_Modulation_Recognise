import hardware_sim as sim
import numpy as np
from scipy import signal
from typing import cast

FS_ANA = 262.144e6
N_ANA = 1048576
FS = 1.024e6
N = 4096
DDS_AMP = 0.75  # 0.75倍归一化幅值


def band_max_peak_find(
    amp_spectrum: np.ndarray,
    freq_start: float,
    freq_end: float,
    fs_hz: float,
    n_pts: int,
) -> float:
    bin_start = int(freq_start / (fs_hz / n_pts))
    bin_end = int(freq_end / (fs_hz / n_pts))
    sum = 0
    for i in amp_spectrum[bin_start : bin_end + 1]:
        sum += i
    return sum


def mix_freq(freq_lo: float, seq: np.ndarray, n_pts: int) -> np.ndarray:
    dds_seq = sim.dds(freq_lo, DDS_AMP, FS_ANA, N_ANA)  # dds生成LO频率信号
    mix_seq = sim.mix(seq, dds_seq)  # 进行混频
    filtered_seq = sim.aalpf_300khz(mix_seq, FS)  # 过抗混滤波器
    filtered_seq_gained = sim.gain(filtered_seq, 1)
    sampled_seq = signal.resample(filtered_seq_gained, n_pts)
    return cast(np.ndarray, sampled_seq)


def find_best_lo(
    original_seq: np.ndarray,
    lo_start: float,
    lo_end: float,
    lo_step: float,
    freq_start: float,
    freq_end: float,
    fs_hz: float,
    n_pts: int,
) -> float:
    peak_val_list = []
    for i in np.arange(lo_start, lo_end + lo_step, lo_step):
        sampled_seq = mix_freq(i, original_seq, n_pts=n_pts)
        amp_spectrum = np.abs(np.fft.fft(sampled_seq))
        max_val = band_max_peak_find(
            amp_spectrum=amp_spectrum,
            freq_start=freq_start,
            freq_end=freq_end,
            fs_hz=fs_hz,
            n_pts=n_pts,
        )
        peak_val_list.append(max_val)
    peak_val_list = np.array(peak_val_list)
    max_index = np.argmax(peak_val_list)
    lo_best = lo_start + max_index * lo_step
    return lo_best
