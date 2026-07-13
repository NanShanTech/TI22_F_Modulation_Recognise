import numpy as np


def am_modulation(
    fs_hz: float,
    n_pts: int,
    carrier_freq: float,
    carrier_amp: float,
    modulation_freq: float,
    modulation_amp: float,
) -> np.ndarray:
    Ts = 1 / fs_hz
    time_seq = np.arange(0, 1, Ts)
    time_seq = time_seq[0:n_pts]
    carrier_seq = carrier_amp * np.sin(2 * np.pi * carrier_freq * time_seq)
    modulation_seq = modulation_amp * np.sin(2 * np.pi * modulation_freq * time_seq)
    am_signal = (1 + modulation_seq) * carrier_seq
    return am_signal


def fm_modulation(
    fs_hz: float,
    n_pts: int,
    carrier_freq: float,
    carrier_amp: float,
    modulation_freq: float,
    modulation_amp: float,
    kf: float = 10000000,
) -> np.ndarray:
    Ts = 1 / fs_hz
    time_seq = np.arange(0, 1, Ts)
    time_seq = time_seq[0:n_pts]
    modulation_seq = modulation_amp * np.sin(2 * np.pi * carrier_freq * time_seq)
    modulation_integral = np.cumsum(modulation_seq) / fs_hz
    fm_signal = np.cos(
        2 * np.pi * carrier_freq * time_seq + 2 * np.pi * kf * modulation_integral
    )
    return fm_signal
