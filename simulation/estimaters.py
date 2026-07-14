import numpy as np
from dataclasses import dataclass
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import windows


@dataclass
class EstimateData:
    freq_estimated: float
    amplitude_estimated: float
    phase_estimated: float = 0.0

def _no_interp(#没有插值算法,模拟不加插值算法的情形
        spectrum: np.ndarray, #复数谱
        peak_bin: int,
        fs_hz: float,
        n_pts: int,
        coherent_gain:float
        ) -> EstimateData:
    if peak_bin > n_pts // 2:
        peak_bin = (n_pts // 2) - peak_bin
    output_EstimateData = EstimateData(
            freq_estimated=peak_bin * (fs_hz / n_pts),
            amplitude_estimated=np.abs(spectrum[peak_bin]) / (n_pts * coherent_gain),
            phase_estimated=np.angle(spectrum[peak_bin])
            )
    return output_EstimateData

def _estimate_phase(
        angle: float, #该点辐角
        freq_delta: float, #频差δ
        n_pts: int #fft点数
        ):
    return angle - np.pi * freq_delta * (n_pts - 1) / n_pts

def _parabolic_interp( #抛物线插值法
                    spectrum: np.ndarray, #复数谱
                     peak_bin: int, #峰值bin
                     fs_hz: float,
                     n_pts: int,
                     coherent_gain: float #相干增益
                     ) -> EstimateData:
    y_minus1 = spectrum[peak_bin - 1]
    y_minus1 = np.abs(y_minus1)
    y_0 = spectrum[peak_bin]
    y_0 = np.abs(y_0)
    y_plus1 = spectrum[peak_bin + 1]
    y_plus1 = np.abs(y_plus1)
    freq_delta = 0.5 * (y_minus1 - y_plus1) / (y_minus1 - 2 * y_0 + y_plus1)
    a = (y_minus1 + y_plus1 - 2 * y_0)#抛物线方程中的a
    y_peak = y_0 - a * (freq_delta ** 2)
    if peak_bin > n_pts // 2:
        peak_bin = (n_pts // 2) - peak_bin
    freq_estimated= (peak_bin + freq_delta) * (fs_hz / n_pts)
    amplitude_estimated = y_peak / (n_pts * coherent_gain)
    angle_ori = np.angle(spectrum[peak_bin])
    
    phase_angle = _estimate_phase(
            angle=angle_ori,
            freq_delta=freq_delta,
            n_pts=n_pts
            )

    output_EstimateData = EstimateData(
            freq_estimated=freq_estimated,
            amplitude_estimated=amplitude_estimated,
            phase_estimated=phase_angle
            )
    
    return output_EstimateData
    
def _delta_rect_candan( #candan插值算法
        spectrum_complex: np.ndarray, #candan插值算法需要复数谱
        peak_bin: int, #峰值bin
        ) -> float:
    y_minus1 = spectrum_complex[peak_bin - 1]
    y_0 = spectrum_complex[peak_bin]
    y_plus1 = spectrum_complex[peak_bin + 1]
    freq_delta = np.real((y_minus1 - y_plus1) / (2 * y_0 - y_minus1 -y_plus1))
    return freq_delta

def _get_r_and_alpha( #获得r和alpha(频率修正公式中的常用变量),增加代码复用
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> tuple[int,float]:
    y_minus1 = np.abs(spectrum_complex[peak_bin-1])
    y_0 = np.abs(spectrum_complex[peak_bin])
    y_plus1 = np.abs(spectrum_complex[peak_bin+1])
    r = 1 if y_plus1 >= y_minus1 else -1
    alpha = np.abs(spectrum_complex[peak_bin+r]) / y_0
    return r, float(alpha)

#以下为频差估计算法
def _delta_rect_rife(
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> float:
    r, alpha = _get_r_and_alpha(spectrum_complex, peak_bin)
    return r * alpha / (1 + alpha)

def _delta_hann_grandke(
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> float:
    r, alpha = _get_r_and_alpha(spectrum_complex, peak_bin)
    return r * (2 * alpha - 1) / (alpha + 1)

def _delta_hamming_offelli(
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> float:
    r, alpha = _get_r_and_alpha(spectrum_complex, peak_bin)
    return r * (alpha - 0.54) / (0.46 * alpha + 0.54)

def _delta_blackman_andria(
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> float:
    r, alpha = _get_r_and_alpha(spectrum_complex, peak_bin)
    return r * (3 * alpha - 1.5) / (alpha + 1.5)

def _delta_blackmanharris_agrez(
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> float:
    r, alpha = _get_r_and_alpha(spectrum_complex, peak_bin)
    return r * (4 * alpha - 2) / (alpha + 2)

def _delta_flattop_andria( #注:平顶窗的频率估计不可靠
        spectrum_complex: np.ndarray,
        peak_bin: int
        ) -> float:
    r, alpha = _get_r_and_alpha(spectrum_complex, peak_bin)
    return r * (5.5 * alpha - 4.5) / (alpha + 1)

#以下为幅值估计算法
_sinc_delta = lambda delta: np.pi * delta / np.sin(np.pi * delta)
_m_plus_n_delta_square = lambda m,n,delta: m + n * delta * delta
def _amp_rect(
        x_k0: float,#该频点fft幅值绝对值
        freq_delta: float #频率差δ
        ) -> float:
    return x_k0 * _sinc_delta(freq_delta)

def _amp_hann(
        x_k0: float,
        freq_delta: float
        ) -> float:
    return x_k0 * _sinc_delta(freq_delta) * _m_plus_n_delta_square(1, -1, freq_delta)

def _amp_hamming(
        x_k0: float,
        freq_delta: float
        ) -> float:
    return x_k0 * _sinc_delta(freq_delta) * (_m_plus_n_delta_square(1, -1, freq_delta) / 
                                                 _m_plus_n_delta_square(1, -0.148, freq_delta))

def _amp_blackman(
        x_k0: float,
        freq_delta: float
        ) -> float:
    return x_k0 * _sinc_delta(freq_delta) * (_m_plus_n_delta_square(1, -1, freq_delta) *
                                             _m_plus_n_delta_square(4, -1, freq_delta) /
                                             _m_plus_n_delta_square(4, -1.69, freq_delta))

def _amp_blackmanharris(
        x_k0: float,
        freq_delta: float
        ) -> float:
    return x_k0 * _sinc_delta(freq_delta) * (_m_plus_n_delta_square(1, -1, freq_delta) * 
                                                 _m_plus_n_delta_square(1, -0.25, freq_delta) *
                                                 _m_plus_n_delta_square(1, -0.111, freq_delta))

def _amp_flattop(
        x_k0: float,
        freq_delta: float
        ) -> float:
    return x_k0 * (1 + 0.02 * freq_delta ** 2 + 0.0005 * freq_delta ** 4)

_DELTA_METHODS = {
        "rife": _delta_rect_rife,
        "candan": _delta_rect_candan,
        "granke": _delta_hann_grandke,
        "offelli": _delta_hamming_offelli,
        "andria_blackman": _delta_blackman_andria,
        "agrez": _delta_blackmanharris_agrez,
        "andria_flattop": _delta_flattop_andria
        }

_AMP_METHODS = {
        "rect": _amp_rect,
        "hann": _amp_hann,
        "hamming": _amp_hamming,
        "blackman": _amp_blackman,
        "blackmanharris": _amp_blackmanharris,
        "flattop": _amp_flattop
        }

_AUTO_DELTA_METHODS = { #自动窗函数选择表
        "rect": "candan",
        "hann": "granke",
        "hamming": "offelli",
        "blackman": "andria_blackman",
        "blackmanharris": "agrez",
        "flattop": "andria_flattop"
        }

_WINDOW_CHECK_LIST = { #查看用户是否选择错了窗函数算法
        "rect": ["rife", "candan"],
        "hann": ["granke"],
        "hamming": ["offelli"],
        "blackman": ["andria_blackman"],
        "blackmanharris": ["agrez"],
        "flattop": ["andria_flattop"]
        }

#检查算法是否对应窗函数,防止用户选择错误
def check_algorithm_in_list(window_type: str, algorithm_type: str) -> bool:
    for i in _WINDOW_CHECK_LIST[window_type]:
        if i == algorithm_type:
            return True
    return False

#统一抽象接口
def estimate_freq_amplitude_phase(
        spectrum_complex: np.ndarray, #复数谱
        peak_bin: int, #峰值bin
        fs_hz: float, #采样率
        window_type: str, #窗函数名称
        # rect | hann | hamming | blackman | blackmanharris | flattop
        algorithm_type: str #详情见不同窗函数可适用的频率插值算法表
        ) -> EstimateData:
    n_pts = len(spectrum_complex) #CMSIS-DSP上应该改为 len(spectrum_complex) / 2
    if peak_bin <= 0 or peak_bin > n_pts - 2:
        print("[ERROR] peak bin is too small or too large")
        return EstimateData(
                freq_estimated=float(np.nan),
                amplitude_estimated=float(np.nan),
                phase_estimated=float(np.nan)
                )
    window = windows.generate_window(window_type, n_pts)
    coherent_gain = window.coherent_gain
    #处理auto和之前遗留的抛物线/不加估计算法
    x_k0 = np.abs(spectrum_complex[peak_bin])
    x_k0_angle = np.angle(spectrum_complex[peak_bin])
    method_type = ""
    if algorithm_type == "_no_interp":
        return _no_interp(
                spectrum=spectrum_complex,
                peak_bin=peak_bin,
                fs_hz=fs_hz,
                n_pts=n_pts,
                coherent_gain=coherent_gain
                )
    elif algorithm_type == "parabolic":
        return _parabolic_interp(
                spectrum=spectrum_complex,
                peak_bin=peak_bin,
                fs_hz=fs_hz,
                n_pts=n_pts,
                coherent_gain=coherent_gain
                )
    elif algorithm_type == "auto":
        method_type = _AUTO_DELTA_METHODS[window_type]
    
    elif check_algorithm_in_list(window_type, algorithm_type):
        method_type = algorithm_type
    else:
        print("[ERROR] the algorithm does not fit the windows function")
        return EstimateData(#出错返回nan
                freq_estimated=float(np.nan),
                amplitude_estimated=(np.nan),
                phase_estimated=(np.nan)
                )

    estimated_freq_delta = _DELTA_METHODS[method_type](spectrum_complex, peak_bin)
    if peak_bin > n_pts // 2:
        peak_bin = (n_pts // 2) - peak_bin
    estimated_freq = (peak_bin + estimated_freq_delta) * (fs_hz / n_pts)
    estimated_amplitude = _AMP_METHODS[window_type](x_k0, estimated_freq_delta)
    estimated_amplitude /= n_pts * coherent_gain
    estimated_phase = _estimate_phase(angle=x_k0_angle,
                                        freq_delta=estimated_freq_delta,
                                        n_pts=n_pts)
    return EstimateData(
            freq_estimated=estimated_freq,
            amplitude_estimated=estimated_amplitude,
            phase_estimated=estimated_phase
            )

