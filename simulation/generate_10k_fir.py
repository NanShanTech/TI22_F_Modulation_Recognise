#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any, cast

import numpy as np
from numpy.typing import NDArray
from scipy import signal


FS_INPUT = 1.024e6
DECIMATION = 8
FS_OUTPUT = FS_INPUT / DECIMATION

F_PASS = 12e3
F_STOP = 40e3
ATTENUATION_DB = 60.0


def design_lpf_decimate_128k() -> NDArray[np.float64]:
    """
    设计用于1.024 MHz -> 128 kHz八倍抽取的抗混叠FIR低通滤波器。

    返回值为常规时序系数：
        [b[0], b[1], ..., b[num_taps - 1]]

    注意：
        CMSIS-DSP需要时间反序系数，导出C头文件时会自动反转。
    """

    if DECIMATION <= 1:
        raise ValueError("抽取倍数必须大于1")

    if not (0.0 < F_PASS < F_STOP < FS_INPUT / 2.0):
        raise ValueError("滤波器频率参数不合法")

    if F_STOP >= FS_OUTPUT / 2.0:
        raise ValueError("当前设计要求阻带起始频率必须低于抽取后奈奎斯特频率")

    normalized_width = (F_STOP - F_PASS) / (FS_INPUT / 2.0)

    num_taps_raw, beta_raw = signal.kaiserord(
        ripple=ATTENUATION_DB,
        width=normalized_width,
    )

    beta = float(beta_raw)

    # 调整成：
    # num_taps = K * (2 * DECIMATION) + 1
    #
    # 对M=8而言，num_taps满足16K+1。
    # 保持奇数长度和Type-I线性相位结构。
    alignment = 2 * DECIMATION
    num_taps = int(np.ceil((int(num_taps_raw) - 1) / alignment)) * alignment + 1

    cutoff = 0.5 * (F_PASS + F_STOP)
    window_spec: Any = ("kaiser", beta)

    coeffs = signal.firwin(
        numtaps=num_taps,
        cutoff=cutoff,
        window=window_spec,
        pass_zero=True,
        fs=FS_INPUT,
        scale=True,
    )

    return cast(NDArray[np.float64], coeffs)


def analyze_filter(
    coeffs: NDArray[np.float64],
) -> dict[str, float]:
    """
    计算滤波器关键频率指标。
    """

    frequency, response = signal.freqz(
        coeffs,
        worN=262144,
        fs=FS_INPUT,
    )

    magnitude = np.abs(response)
    magnitude_db = 20.0 * np.log10(np.maximum(magnitude, np.finfo(np.float64).tiny))

    passband = frequency <= F_PASS
    stopband = frequency >= F_STOP

    passband_db = magnitude_db[passband]
    stopband_db = magnitude_db[stopband]

    passband_ripple_db = float(np.max(passband_db) - np.min(passband_db))

    passband_min_db = float(np.min(passband_db))
    stopband_max_db = float(np.max(stopband_db))
    stopband_attenuation_db = -stopband_max_db

    output_nyquist_index = int(np.argmin(np.abs(frequency - FS_OUTPUT / 2.0)))
    output_nyquist_db = float(magnitude_db[output_nyquist_index])

    group_delay_input_samples = 0.5 * (len(coeffs) - 1)
    group_delay_seconds = group_delay_input_samples / FS_INPUT
    group_delay_output_samples = group_delay_seconds * FS_OUTPUT

    return {
        "num_taps": float(len(coeffs)),
        "passband_ripple_db": passband_ripple_db,
        "passband_min_db": passband_min_db,
        "stopband_max_db": stopband_max_db,
        "stopband_attenuation_db": stopband_attenuation_db,
        "output_nyquist_db": output_nyquist_db,
        "group_delay_input_samples": group_delay_input_samples,
        "group_delay_output_samples": group_delay_output_samples,
        "group_delay_us": group_delay_seconds * 1e6,
    }


def format_c_float(value: np.float32) -> str:
    """
    将float32系数格式化为C语言浮点常量。
    """

    return f"{float(value):+.9e}f"


def generate_c_header(
    coeffs: NDArray[np.float64],
    block_size: int,
    output_file: Path,
) -> None:
    """
    生成CMSIS-DSP arm_fir_decimate_f32使用的C头文件。
    """

    if block_size <= 0:
        raise ValueError("block_size必须大于0")

    if block_size % DECIMATION != 0:
        raise ValueError(f"block_size必须是抽取倍数{DECIMATION}的整数倍")

    num_taps = len(coeffs)
    state_length = num_taps + block_size - 1
    output_block_size = block_size // DECIMATION

    # CMSIS-DSP要求滤波器系数按时间反序存储。
    cmsis_coeffs = np.asarray(coeffs[::-1], dtype=np.float32)

    coefficient_lines: list[str] = []

    coefficients_per_line = 4
    for index in range(0, num_taps, coefficients_per_line):
        values = cmsis_coeffs[index : index + coefficients_per_line]
        formatted = ", ".join(format_c_float(value) for value in values)

        if index + coefficients_per_line < num_taps:
            formatted += ","

        coefficient_lines.append(f"    {formatted}")

    header = f"""\
#ifndef DECIMATOR_128K_COEFFS_H
#define DECIMATOR_128K_COEFFS_H

#include "arm_math.h"

#define DECIMATOR_128K_M                  ({DECIMATION}U)
#define DECIMATOR_128K_NUM_TAPS           ({num_taps}U)
#define DECIMATOR_128K_INPUT_BLOCK_SIZE   ({block_size}U)
#define DECIMATOR_128K_OUTPUT_BLOCK_SIZE  ({output_block_size}U)
#define DECIMATOR_128K_STATE_LENGTH       ({state_length}U)

static const float32_t decimator_128k_coeffs[
    DECIMATOR_128K_NUM_TAPS
] = {{
{chr(10).join(coefficient_lines)}
}};

#endif
"""

    output_file.write_text(header, encoding="utf-8")


def print_analysis(
    analysis: dict[str, float],
    block_size: int,
) -> None:
    """
    输出滤波器分析报告。
    """

    print("滤波器设计结果")
    print("------------------------------")
    print(f"输入采样率             : {FS_INPUT:.0f} Hz")
    print(f"抽取倍数               : {DECIMATION}")
    print(f"输出采样率             : {FS_OUTPUT:.0f} Hz")
    print(f"通带边缘               : {F_PASS:.0f} Hz")
    print(f"阻带起始               : {F_STOP:.0f} Hz")
    print(f"目标阻带衰减           : {ATTENUATION_DB:.2f} dB")
    print(f"滤波器抽头数           : {int(analysis['num_taps'])}")
    print(f"通带峰峰纹波           : {analysis['passband_ripple_db']:.6f} dB")
    print(f"通带最小增益           : {analysis['passband_min_db']:.6f} dB")
    print(f"实测最差阻带衰减       : {analysis['stopband_attenuation_db']:.3f} dB")
    print(f"64 kHz处增益           : {analysis['output_nyquist_db']:.3f} dB")
    print(
        f"群延迟                 : "
        f"{analysis['group_delay_input_samples']:.1f}个输入样点"
    )
    print(
        f"群延迟                 : "
        f"{analysis['group_delay_output_samples']:.3f}个输出样点"
    )
    print(f"群延迟                 : {analysis['group_delay_us']:.3f} us")
    print(f"CMSIS输入块长度        : {block_size}")
    print(f"CMSIS输出块长度        : {block_size // DECIMATION}")
    print(f"CMSIS状态数组长度      : {int(analysis['num_taps']) + block_size - 1}")

    if analysis["stopband_attenuation_db"] < ATTENUATION_DB:
        print("警告：实测阻带衰减未达到目标值，应增加抽头数或改用等波纹设计。")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=("生成CMSIS-DSP 1.024 MHz到128 kHz八倍抽取FIR滤波器")
    )

    parser.add_argument(
        "--block-size",
        type=int,
        default=256,
        help="每次传给CMSIS-DSP的输入样点数，必须是8的整数倍",
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path("decimator_128k_coeffs.h"),
        help="输出C头文件路径",
    )

    args = parser.parse_args()

    coeffs = design_lpf_decimate_128k()
    analysis = analyze_filter(coeffs)

    generate_c_header(
        coeffs=coeffs,
        block_size=args.block_size,
        output_file=args.output,
    )

    print_analysis(
        analysis=analysis,
        block_size=args.block_size,
    )

    print("------------------------------")
    print(f"已生成头文件：{args.output.resolve()}")


if __name__ == "__main__":
    main()
