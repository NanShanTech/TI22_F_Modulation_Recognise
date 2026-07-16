#!/usr/bin/env python3
"""
解析 STM32H7 Memory Inspector dump 的 Intel HEX 文件，
提取 float32 数组并绘制 ADC 采样波形。

采样参数:
  fs = 1.024 MHz
  N  = 4096 pts (文件中可能只有部分数据,根据实际大小自动确定)

使用方法:
  python plot_adc_hex.py <hex_file>
  或直接运行 (默认使用 adc_buffer_2544.hex)
"""

import sys
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
from scipy import signal
from typing import Any, cast


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


def parse_intel_hex(filepath):
    """
    解析 Intel HEX 文件,返回连续的原始字节数据。

    对于从 STM32 调试器 dump 的 HEX,数据记录的不连续地址段会被
    跳过并记录偏移。最终返回最小连续覆盖的 bytes。
    """
    base_addr = 0
    # 收集 (absolute_address, payload_bytes) 列表
    chunks: list[tuple[int, bytes]] = []

    with open(filepath, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line[0] != ":":
                continue

            byte_count = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)

            if record_type == 0x04:  # Extended Linear Address
                base_addr = int(line[9:13], 16) << 16
            elif record_type == 0x00:  # Data Record
                payload = bytes.fromhex(line[9 : 9 + byte_count * 2])
                abs_addr = base_addr + address
                chunks.append((abs_addr, payload))
            elif record_type == 0x01:  # EOF
                break

    if not chunks:
        raise ValueError("HEX 文件中没有数据记录")

    # 排序并找到最小连续范围
    chunks.sort(key=lambda x: x[0])
    start = chunks[0][0]
    end = max(addr + len(payload) for addr, payload in chunks)

    # 填充数据
    data = bytearray(end - start)
    for addr, payload in chunks:
        offset = addr - start
        data[offset : offset + len(payload)] = payload

    print(f"  数据起始绝对地址: 0x{start:08X}")
    print(f"  数据结束绝对地址: 0x{end:08X}")
    print(f"  连续数据长度: {len(data)} 字节 ({len(data) // 4} float32)")

    return bytes(data)


def main():
    if len(sys.argv) > 1:
        hex_file = sys.argv[1]
    else:
        hex_file = "adc_buffer_2544.hex"

    hex_path = (
        Path(__file__).parent / hex_file
        if not Path(hex_file).is_absolute()
        else Path(hex_file)
    )
    print(f"正在解析 HEX 文件: {hex_path}")
    raw_data = parse_intel_hex(str(hex_path))

    # 解析为 float32 (小端)
    samples = np.frombuffer(raw_data, dtype="<f4")
    samples = lpf_100k(samples)
    samples = np.abs(np.fft.fft(samples))
    print(f"float32 样本数: {len(samples)}")

    if len(samples) == 0:
        print("错误: 没有解析到任何样本!")
        sys.exit(1)

    # 采样参数
    fs = 1.024e6  # 1.024 MHz
    ts = 1.0 / fs  # 采样周期 (秒)

    t_sec = np.arange(len(samples)) * ts  # 时间轴 (秒)
    t_us = t_sec * 1e6  # 时间轴 (微秒)

    # ============ 绘图 ============
    plt.style.use("seaborn-v0_8-darkgrid")
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 5))

    # --- 左图: 整体波形 ---
    ax1.plot(t_us, samples, linewidth=0.6, color="#1f77b4")
    ax1.set_xlabel("Time (μs)")
    ax1.set_ylabel("Amplitude")
    ax1.set_title(
        f"ADC Waveform — Full View\n{len(samples)} samples @ {fs / 1e6:.3f} MHz"
    )
    ax1.grid(True, alpha=0.3)

    # 统计信息文本框
    stats_text = (
        f"Samples: {len(samples)}\n"
        f"Max: {samples.max():.4f}\n"
        f"Min: {samples.min():.4f}\n"
        f"Mean: {samples.mean():.4f}\n"
        f"RMS:  {np.sqrt(np.mean(samples**2)):.4f}"
    )
    ax1.annotate(
        stats_text,
        xy=(0.97, 0.95),
        xycoords="axes fraction",
        ha="right",
        va="top",
        fontsize=9,
        bbox=dict(boxstyle="round,pad=0.5", facecolor="wheat", alpha=0.8),
    )

    # --- 右图: 前 200 个样本的细节 ---
    n_preview = min(200, len(samples))
    ax2.plot(
        t_us[:n_preview],
        samples[:n_preview],
        linewidth=1.2,
        color="#d62728",
        marker=".",
        markersize=2,
    )
    ax2.set_xlabel("Time (μs)")
    ax2.set_ylabel("Amplitude")
    ax2.set_title(f"ADC Waveform — First {n_preview} Samples Detail")
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()

    # ============ 打印统计 ============
    duration = len(samples) * ts
    print(f"\n统计信息:")
    print(f"  样本数     = {len(samples)}")
    print(f"  采样率     = {fs / 1e6:.3f} MHz")
    print(f"  采样周期   = {ts * 1e6:.3f} μs")
    print(f"  总时长     = {duration * 1e6:.2f} μs ({duration * 1e3:.2f} ms)")
    print(f"  Max        = {samples.max():.6f}")
    print(f"  Min        = {samples.min():.6f}")
    print(f"  Peak-Peak  = {samples.max() - samples.min():.6f}")
    print(f"  Mean       = {samples.mean():.6f}")
    print(f"  RMS        = {np.sqrt(np.mean(samples**2)):.6f}")


if __name__ == "__main__":
    main()
