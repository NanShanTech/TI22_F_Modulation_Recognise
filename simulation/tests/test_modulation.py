"""test_modulation.py — AM/FM 调制信号定量验证测试"""

import numpy as np
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import modulation


# ========== 测试参数 ==========
FS_HZ = 100e6       # 采样率 100 MHz
N_PTS = 131072      # 采样点数
CARRIER_FREQ = 10e6 # 载波频率 10 MHz
CARRIER_AMP = 1.0   # 载波幅度 1 V
MOD_FREQ = 3e3      # 调制频率 3 kHz
MOD_AMP = 0.1       # 调制幅度 0.1 (即 AM 调制指数 / FM 频偏系数)


def test_am_timedomain_envelope():
    """验证 AM 时域包络在正确范围内"""
    seq = modulation.am_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP,
    )
    Ts = 1.0 / FS_HZ
    t = np.arange(N_PTS) * Ts

    # AM 包络 = |1 + A_m * sin(2π * f_m * t)|
    analytic_env = np.abs(1 + MOD_AMP * np.sin(2 * np.pi * MOD_FREQ * t))

    # 实际信号的幅度应被包络约束
    assert np.all(np.abs(seq) <= analytic_env.max() * CARRIER_AMP + 0.01), \
        "信号幅度不应超过包络最大值"
    assert analytic_env.min() == 1 - MOD_AMP, \
        f"包络最小值应为 1 - A_m = {1 - MOD_AMP}"
    assert analytic_env.max() == 1 + MOD_AMP, \
        f"包络最大值应为 1 + A_m = {1 + MOD_AMP}"


def test_am_spectrum_components():
    """验证 AM 频谱包含载波和上下边带"""
    seq = modulation.am_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP,
    )
    freq = np.linspace(-FS_HZ / 2, FS_HZ / 2, N_PTS)
    fft_mag = np.abs(np.fft.fftshift(np.fft.fft(seq, N_PTS)))

    # 期望的主频率分量：载波和 ±3 kHz 边带
    expected_freqs = [
        CARRIER_FREQ - MOD_FREQ,  # 下边带
        CARRIER_FREQ,              # 载波
        CARRIER_FREQ + MOD_FREQ,  # 上边带
    ]
    freq_res = FS_HZ / N_PTS  # ~762.9 Hz
    for ef in expected_freqs:
        bin_idx = np.argmin(np.abs(freq - ef))
        assert abs(freq[bin_idx] - ef) < freq_res * 1.5, \
            f"频率 {ef/1e6:.4f} MHz 应在分辨率带宽 {freq_res:.1f} Hz 内找到"

    # 验证载波幅度 ~ N_PTS * carrier_amp / 2
    carrier_bin = np.argmin(np.abs(freq - CARRIER_FREQ))
    carrier_mag = fft_mag[carrier_bin]
    expected_carrier_mag = N_PTS * CARRIER_AMP / 2
    assert abs(carrier_mag - expected_carrier_mag) < expected_carrier_mag * 0.1, \
        f"载波幅度 {carrier_mag:.1f} 应接近理论值 {expected_carrier_mag:.1f}"

    # 验证边带幅度 ~ carrier_mag * MOD_AMP
    lsb_bin = np.argmin(np.abs(freq - (CARRIER_FREQ - MOD_FREQ)))
    lsb_mag = fft_mag[lsb_bin]
    expected_sb_mag = expected_carrier_mag * MOD_AMP / 1  # 因为 (1 + m(t)) 展开的系数
    # 实际上 AM 频谱：载波幅度 A_c/2, 边带幅度 A_c*m/4 (单边)
    # s(t) = A_c*cos(ω_c*t) + A_c*m/2*cos((ω_c+ω_m)*t) + A_c*m/2*cos((ω_c-ω_m)*t)
    # 所以边带幅度应为 A_c*m/4 在单边FFT幅度中
    expected_sb_mag_single = expected_carrier_mag * MOD_AMP / 2
    assert lsb_mag > expected_sb_mag_single * 0.5, \
        f"下边带幅度 {lsb_mag:.1f} 应可检测 (> {expected_sb_mag_single*0.5:.1f})"


def test_fm_modulation_freq_correct():
    """验证 FM 调制信号使用 modulation_freq 而非 carrier_freq"""
    seq = modulation.fm_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP, kf=100000,
    )
    Ts = 1.0 / FS_HZ
    t = np.arange(N_PTS) * Ts

    # 构造正确的 FM 信号做参考
    mod_seq = MOD_AMP * np.sin(2 * np.pi * MOD_FREQ * t)
    mod_integral = np.cumsum(mod_seq) / FS_HZ
    ref_seq = CARRIER_AMP * np.cos(
        2 * np.pi * CARRIER_FREQ * t + 2 * np.pi * 100000 * mod_integral
    )

    # 如果调制频率正确，信号应与参考信号非常接近
    # (忽略数值误差带来的微小相位漂移)
    correlation = np.corrcoef(ref_seq, seq)[0, 1]
    assert correlation > 0.99, \
        f"与正确 FM 的相关系数 {correlation:.4f} 应接近 1.0"

    # 对比错误的 FM (用 carrier_freq 做调制) — 验证它们确实不同
    wrong_mod = MOD_AMP * np.sin(2 * np.pi * CARRIER_FREQ * t)
    wrong_integral = np.cumsum(wrong_mod) / FS_HZ
    wrong_seq = CARRIER_AMP * np.cos(
        2 * np.pi * CARRIER_FREQ * t + 2 * np.pi * 100000 * wrong_integral
    )
    wrong_corr = np.corrcoef(wrong_seq, seq)[0, 1]
    assert wrong_corr < 0.5, \
        f"修正后的 FM 应与错误版本明显不同 (wrong_corr={wrong_corr:.4f})"


def test_fm_bandwidth():
    """验证 FM 信号带宽符合卡森准则"""
    seq = modulation.fm_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP, kf=100000,
    )

    freq = np.linspace(-FS_HZ / 2, FS_HZ / 2, N_PTS)
    fft_mag = np.abs(np.fft.fftshift(np.fft.fft(seq, N_PTS)))

    # 99% 能量带宽
    pos_idx = freq >= 0
    freq_pos = freq[pos_idx]
    mag_pos = fft_mag[pos_idx]
    cumsum = np.cumsum(mag_pos ** 2) / np.sum(mag_pos ** 2)
    bw_low = freq_pos[np.where(cumsum >= 0.005)[0][0]]
    bw_high = freq_pos[np.where(cumsum >= 0.995)[0][0]]
    bw_99 = bw_high - bw_low

    # 卡森带宽: 2 * (Δf + f_m)
    df = 100000 * MOD_AMP  # kf * modulation_amp
    carson_bw = 2 * (df + MOD_FREQ)

    assert bw_99 < carson_bw * 1.5, \
        f"99% 带宽 {bw_99/1e3:.1f} kHz 应接近卡森带宽 {carson_bw/1e3:.1f} kHz"
    assert bw_99 > carson_bw * 0.5, \
        f"99% 带宽 {bw_99/1e3:.1f} kHz 不应远小于卡森带宽 {carson_bw/1e3:.1f} kHz"


def test_fm_carrier_amp_used():
    """验证 FM 使用了 carrier_amp 参数"""
    seq_high = modulation.fm_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, 2.0, MOD_FREQ, MOD_AMP, kf=100000,
    )
    seq_low = modulation.fm_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, 0.5, MOD_FREQ, MOD_AMP, kf=100000,
    )

    # 高振幅信号的幅度应显著大于低振幅信号
    assert np.max(np.abs(seq_high)) > np.max(np.abs(seq_low)) * 1.5, \
        "carrier_amp 大的信号应有更大的幅度"


def test_am_fm_spectra_differ():
    """验证相同参数下 AM 和 FM 频谱特征不同"""
    am_seq = modulation.am_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP,
    )
    fm_seq = modulation.fm_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP, kf=100000,
    )

    # AM 和 FM 不应相同
    assert not np.allclose(am_seq, fm_seq), "AM 和 FM 信号不应相同"

    # 频谱形状应不同
    freq = np.linspace(-FS_HZ / 2, FS_HZ / 2, N_PTS)
    am_fft = np.abs(np.fft.fftshift(np.fft.fft(am_seq, N_PTS)))
    fm_fft = np.abs(np.fft.fftshift(np.fft.fft(fm_seq, N_PTS)))
    correlation_spectrum = np.corrcoef(am_fft, fm_fft)[0, 1]
    assert correlation_spectrum < 0.9, \
        f"AM 和 FM 频谱相关系数 {correlation_spectrum:.3f} 应明显不同"


# ========== 可选的可视化 (仅当直接运行时) ==========
if __name__ == "__main__":
    import matplotlib
    matplotlib.use("Agg")  # 非交互后端，避免阻塞
    import matplotlib.pyplot as plt

    # AM 时域+频域
    am_seq = modulation.am_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP,
    )
    fm_seq = modulation.fm_modulation(
        FS_HZ, N_PTS, CARRIER_FREQ, CARRIER_AMP, MOD_FREQ, MOD_AMP, kf=100000,
    )

    Ts = 1.0 / FS_HZ
    t = np.arange(N_PTS) * Ts
    freq = np.linspace(-FS_HZ / 2, FS_HZ / 2, N_PTS)

    plt.figure(figsize=(12, 8))

    plt.subplot(2, 2, 1)
    # 只显示前500个点以便看清波形
    plt.plot(t[:500] * 1e6, am_seq[:500])
    plt.title("AM 时域 (前500样本)")
    plt.xlabel("时间 (μs)")
    plt.ylabel("幅度 (V)")

    plt.subplot(2, 2, 2)
    am_fft = np.abs(np.fft.fftshift(np.fft.fft(am_seq, N_PTS)))
    plt.plot(freq / 1e6, am_fft)
    plt.title("AM 频谱")
    plt.xlabel("频率 (MHz)")
    plt.xlim(CARRIER_FREQ / 1e6 - 0.05, CARRIER_FREQ / 1e6 + 0.05)

    plt.subplot(2, 2, 3)
    plt.plot(t[:500] * 1e6, fm_seq[:500])
    plt.title("FM 时域 (前500样本)")
    plt.xlabel("时间 (μs)")
    plt.ylabel("幅度 (V)")

    plt.subplot(2, 2, 4)
    fm_fft = np.abs(np.fft.fftshift(np.fft.fft(fm_seq, N_PTS)))
    plt.plot(freq / 1e6, fm_fft)
    plt.title("FM 频谱")
    plt.xlabel("频率 (MHz)")
    plt.xlim(CARRIER_FREQ / 1e6 - 0.05, CARRIER_FREQ / 1e6 + 0.05)

    plt.tight_layout()
    plt.savefig("test_modulation.png", dpi=150)
    print("可视化已保存到 test_modulation.png")
