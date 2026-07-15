# 模拟DDS,混频器,低通滤波等硬件操作
import numpy as np
from scipy import signal
from typing import cast


def gain(seq: np.ndarray, vpp: float) -> np.ndarray:  # 模拟增益
    abs_seq = np.abs(seq)
    max = np.max(abs_seq) + 1e-300  # 防止除以0
    seq /= max
    seq *= vpp / 2
    return seq


def dds(freq: float, amp: float, fs: float, n: int) -> np.ndarray:  # 模拟dds生成正弦波
    Ts = 1 / fs
    time_seq = np.arange(n) * Ts
    return amp * np.cos(2 * np.pi * freq * time_seq)


def mix(seq1: np.ndarray, seq2: np.ndarray) -> np.ndarray:  # 混频器
    return seq1 * seq2


def aalpf_300khz(seq: np.ndarray, fs: float = 262.144e6) -> np.ndarray:
    filter_order = 10
    cutoff_3db = 300e3
    sos = signal.butter(
        N=filter_order,
        Wn=cutoff_3db,
        btype="lowpass",
        fs=fs,
        output="sos",
    )

    filtered_seq = signal.sosfilt(sos, seq)
    return cast(np.ndarray, filtered_seq)
