import numpy as np
import scipy
from dataclasses import dataclass

def _measure_coherent_gain (window: np.ndarray) -> float: #求相干增益，用于矫正幅值
    return float(np.mean(window))

def _measure_scalloping_loss (window: np.ndarray,#求扇贝损失，用于检查数据的误差上限
                             N_fft: int,
                             n_sweep=500#扫频步长，越小越精确
                             ) -> float:
    N = len(window)
    n = np.arange(N)
    k0 = N_fft // 4 #远离DC分量
    s = np.sin(2 * np.pi * k0 / N_fft * n)
    X = np.fft.fft(s * window, n=N_fft)
    peak_ref = np.max(np.abs(X))
    min_peak = float('inf')
    peak = 0
    for offset in np.linspace(0, 1.0, n_sweep):
        s = np.sin(2 * np.pi * (k0 + offset) / N_fft * n)
        X = np.fft.fft(s * window,n=N_fft)
        peak = np.max(np.abs(X))
        if peak <= min_peak:
            min_peak = peak
    scalloping_loss = -20 * np.log10(min_peak / peak_ref)
    return float(scalloping_loss)

_WINDOWS_GENERATORS = { #窗函数生成字典
        "rect": lambda pts: np.ones(pts),
        "hann": lambda pts: scipy.signal.windows.hann(pts, sym=False),
        "hamming": lambda pts: scipy.signal.windows.hamming(pts, sym=False),
        "blackmanharris": lambda pts: scipy.signal.windows.blackmanharris(pts, sym=False),
        "flattop": lambda pts: scipy.signal.windows.flattop(pts, sym=False),
        "blackman": lambda pts: scipy.signal.windows.blackman(pts, sym=False)
        }

@dataclass
class WindowData:
    window_array: np.ndarray
    coherent_gain: float
    scalloping_loss: float

#生成窗函数，窗类型接受：rect(矩形窗) | hann |blackman | hamming | blackmanharris | flattop
def generate_window(type: str, pts: int) -> WindowData:
    window_array = _WINDOWS_GENERATORS[type](pts)
    window_coherent_gain = _measure_coherent_gain(window_array)
    window_scalloping_loss = _measure_scalloping_loss(window_array, pts)
    output_WindowData = WindowData(
            window_array=window_array,
            coherent_gain=window_coherent_gain,
            scalloping_loss=window_scalloping_loss
            )
    return output_WindowData
