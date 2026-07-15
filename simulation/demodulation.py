import numpy as np
from scipy import signal
from typing import Any, cast
from dataclasses import dataclass
import numpy as np
import windows
import estimaters


@dataclass
class IQSeq:
    i_seq: np.ndarray
    q_seq: np.ndarray


@dataclass
class DemodulationData:
    freq: float
    amplitude: float
    m: float


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


def get_iq(
    seq: np.ndarray, fc: float, fs_hz: float, n_pts: int
) -> IQSeq:  # 对采集到的信号做iq混频
    Ts = 1 / fs_hz
    time_seq = np.arange(n_pts) * Ts
    x_t = np.cos(2 * np.pi * fc * time_seq)
    y_t = np.sin(2 * np.pi * fc * time_seq)
    i_seq = seq * x_t
    q_seq = seq * y_t
    i_seq_filted = lpf_100k(i_seq)
    q_seq_filted = lpf_100k(q_seq)
    return IQSeq(i_seq=i_seq_filted, q_seq=q_seq_filted)


def get_envelope(iqseq: IQSeq) -> np.ndarray:  # 获取包络
    i_seq = iqseq.i_seq
    q_seq = iqseq.q_seq
    envelope_seq = (i_seq * i_seq) + (q_seq * q_seq)
    envelope_seq = np.sqrt(envelope_seq)
    return envelope_seq


def get_delta_f(iqseq: IQSeq, fs_hz: float) -> np.ndarray:  # 叉积鉴频器
    Ts = 1 / fs_hz
    i_seq = iqseq.i_seq
    q_seq = iqseq.q_seq
    blockSize = len(i_seq)
    freq_seq = []
    for i in range(1, blockSize):
        cross = (i_seq[i - 1] * q_seq[i]) - (i_seq[i] * q_seq[i - 1])
        dot = (i_seq[i - 1] * i_seq[i]) + (q_seq[i - 1] * q_seq[i]) + 1e-6
        delta_phi = cross / dot
        f = delta_phi / (2 * np.pi * Ts)
        freq_seq.append(f)
    return np.array(freq_seq)


def determine_modulation_method(
    envelope_seq: np.ndarray,
    env_cv_gate: float,
    freq_seq: np.ndarray,
    freq_cv_gate: float,
) -> int:
    envelope_mean = np.mean(envelope_seq)
    envelope_std = np.std(envelope_seq)
    envelope_cv = np.abs(envelope_std / envelope_mean)
    freq_mean = np.mean(freq_seq)
    freq_std = np.std(freq_seq)
    freq_cv = np.abs(freq_std / freq_mean)
    if envelope_cv > env_cv_gate:
        return 1  # AM调制
    elif freq_cv > freq_cv_gate:
        return 2  # FM调制
    else:
        return 0  # CW连续波


def demodulation(
    envelope_seq: np.ndarray,
    delta_f_seq: np.ndarray,
    fs_hz: float,
    modulation_type: int,
) -> DemodulationData:
    if modulation_type == 1:
        demodulation_seq = envelope_seq
    elif modulation_type == 2:
        demodulation_seq = delta_f_seq
        demodulation_seq -= np.mean(delta_f_seq)  # 减去均值
    else:
        print("invaild method")
        return DemodulationData(freq=-114514, amplitude=-114514, m=-114514)

    blockSize = len(demodulation_seq)
    window_hann = windows.generate_window(type="hann", pts=blockSize)
    windowed_seq = demodulation_seq * window_hann.window_array
    fft_seq = np.fft.fft(windowed_seq, blockSize)
    fft_seq_abs = np.abs(fft_seq)
    peak_bin = np.argmax(fft_seq_abs[2 : blockSize // 2])
    peak_bin += 2
    peak_bin = int(peak_bin)
    estimated_data = estimaters.estimate_freq_amplitude_phase(
        spectrum_complex=fft_seq,
        peak_bin=peak_bin,
        fs_hz=fs_hz,
        window_type="hann",
        algorithm_type="auto",
    )
    freq_demod = estimated_data.freq_estimated
    residual_freq = freq_demod % 1000
    amp_demod = estimated_data.amplitude_estimated
    amp_demod *= 2
    dc_amp = fft_seq_abs[0]
    dc_amp /= blockSize * window_hann.coherent_gain
    freq_demod -= residual_freq
    if residual_freq > 500:
        freq_demod += 1000
    if modulation_type == 1:
        amp_demod = np.abs(amp_demod) / np.abs(dc_amp)
        ma = amp_demod
    if modulation_type == 2:
        ma = amp_demod / freq_demod
    return DemodulationData(freq=freq_demod, amplitude=amp_demod, m=ma)
