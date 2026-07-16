from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from scipy import signal


# ============================================================
# 固定设计参数
# ============================================================

FS_HZ = 1_024_000.0
FC_HZ = 5_000.0

# 总阶数为2，即一个二阶节/Biquad。
FILTER_ORDER = 2

# STM32每次处理的块长度。
BLOCK_SIZE = 4096

OUTPUT_HEADER = Path("dc_blocker_biquad.h")
OUTPUT_FIGURE = Path("dc_blocker_response.png")


# ============================================================
# SciPy SOS -> CMSIS-DSP DF2T系数
# ============================================================


def scipy_sos_to_cmsis_df2t(sos: np.ndarray) -> np.ndarray:
    """
    将SciPy SOS系数转换为CMSIS-DSP DF2T系数。

    SciPy每级格式：
        [b0, b1, b2, a0, a1, a2]

    CMSIS-DSP每级格式：
        [b0, b1, b2, -a1, -a2]

    返回值使用float32，以便验证STM32实际使用的系数量化结果。
    """
    sos = np.asarray(sos, dtype=np.float64)

    if sos.ndim != 2 or sos.shape[1] != 6:
        raise ValueError("sos必须是形状为(num_stages, 6)的二维数组")

    cmsis = np.empty((sos.shape[0], 5), dtype=np.float64)

    for stage_index, row in enumerate(sos):
        b0, b1, b2, a0, a1, a2 = row

        if not np.isfinite(row).all():
            raise ValueError(f"第{stage_index}级包含NaN或Inf")

        if abs(a0) < np.finfo(np.float64).tiny:
            raise ValueError(f"第{stage_index}级的a0不能为0")

        # 先将分母首项归一化为1。
        b0 /= a0
        b1 /= a0
        b2 /= a0
        a1 /= a0
        a2 /= a0

        # CMSIS-DSP DF2T采用加反馈形式，因此a1、a2取反。
        cmsis[stage_index] = [
            b0,
            b1,
            b2,
            -a1,
            -a2,
        ]

    return cmsis.astype(np.float32)


def cmsis_df2t_to_scipy_sos(cmsis: np.ndarray) -> np.ndarray:
    """
    将CMSIS-DSP系数转换回SciPy SOS格式，用于验证float32量化后的响应。
    """
    cmsis = np.asarray(cmsis, dtype=np.float32)

    if cmsis.ndim != 2 or cmsis.shape[1] != 5:
        raise ValueError("CMSIS系数必须是形状为(num_stages, 5)的二维数组")

    sos = np.zeros((cmsis.shape[0], 6), dtype=np.float64)

    sos[:, 0:3] = cmsis[:, 0:3].astype(np.float64)
    sos[:, 3] = 1.0
    sos[:, 4] = -cmsis[:, 3].astype(np.float64)
    sos[:, 5] = -cmsis[:, 4].astype(np.float64)

    return sos


# ============================================================
# CMSIS-DSP DF2T的Python参考实现
# ============================================================


def cmsis_df2t_reference(
    seq: np.ndarray,
    cmsis_coeffs: np.ndarray,
) -> np.ndarray:
    """
    按CMSIS-DSP DF2T递推式实现Python参考模型。

    用于检查系数顺序和反馈符号，不用于替代STM32上的CMSIS-DSP。
    """
    x = np.asarray(seq, dtype=np.float32)
    coeffs = np.asarray(cmsis_coeffs, dtype=np.float32)

    if x.ndim != 1:
        raise ValueError("输入必须是一维数组")

    if coeffs.ndim != 2 or coeffs.shape[1] != 5:
        raise ValueError("系数必须是形状为(num_stages, 5)的二维数组")

    stage_input = x.copy()

    for stage in range(coeffs.shape[0]):
        b0, b1, b2, a1, a2 = coeffs[stage]

        d1 = np.float32(0.0)
        d2 = np.float32(0.0)

        stage_output = np.empty_like(stage_input)

        for n, sample in enumerate(stage_input):
            y = np.float32(b0 * sample + d1)

            next_d1 = np.float32(b1 * sample + a1 * y + d2)

            next_d2 = np.float32(b2 * sample + a2 * y)

            stage_output[n] = y
            d1 = next_d1
            d2 = next_d2

        stage_input = stage_output

    return stage_input


# ============================================================
# 频率响应与稳定性验证
# ============================================================


def calculate_frequency_response(
    sos: np.ndarray,
    fs_hz: float,
    frequency_points: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """
    优先使用新版freqz_sos；旧版SciPy回退到sosfreqz。
    """
    if hasattr(signal, "freqz_sos"):
        frequencies, response = signal.freqz_sos(
            sos,
            worN=frequency_points,
            fs=fs_hz,
        )
    else:
        frequencies, response = signal.sosfreqz(
            sos,
            worN=frequency_points,
            fs=fs_hz,
        )

    return frequencies, response


def check_float32_stability(sos_float32: np.ndarray) -> None:
    """
    检查float32量化后的每个二阶节极点是否位于单位圆内。
    """
    for stage_index, row in enumerate(sos_float32):
        _, _, _, a0, a1, a2 = row

        poles = np.roots([a0, a1, a2])
        radii = np.abs(poles)

        print(f"\n第{stage_index + 1}级极点：")
        for pole, radius in zip(poles, radii):
            print(f"  pole = {pole}, |pole| = {radius:.9f}")

        if np.any(radii >= 1.0):
            raise RuntimeError(f"第{stage_index + 1}级在float32量化后不稳定")


def print_key_frequency_response(
    sos: np.ndarray,
    fs_hz: float,
) -> None:
    """
    打印关键频率处的幅频响应。
    """
    test_frequencies = np.array(
        [
            0.0,
            100.0,
            1_000.0,
            3_000.0,
            5_000.0,
            10_000.0,
            190_000.0,
            200_000.0,
            210_000.0,
        ],
        dtype=np.float64,
    )

    _, response = calculate_frequency_response(
        sos,
        fs_hz,
        test_frequencies,
    )

    magnitude = np.abs(response)
    magnitude_db = 20.0 * np.log10(np.maximum(magnitude, np.finfo(np.float64).tiny))

    print("\n关键频率响应：")
    print("频率/Hz        幅度/dB")
    print("---------------------------")

    for frequency, gain_db in zip(test_frequencies, magnitude_db):
        print(f"{frequency:10.1f}    {gain_db:12.6f}")


# ============================================================
# 生成CMSIS-DSP头文件
# ============================================================


def format_float32_for_c(value: np.float32) -> str:
    """
    以足够精度输出C语言float常量。
    """
    value = np.float32(value)

    if value == np.float32(0.0):
        return "0.000000000e+00f"

    return f"{float(value):.9e}f"


def generate_cmsis_header(
    cmsis_coeffs: np.ndarray,
    fs_hz: float,
    fc_hz: float,
    filter_order: int,
    output_path: Path,
) -> None:
    """
    生成CMSIS-DSP DF2T使用的C头文件。
    """
    coeffs = np.asarray(cmsis_coeffs, dtype=np.float32)
    num_stages = coeffs.shape[0]

    flat_coeffs = coeffs.reshape(-1)

    coefficient_lines = []

    for stage in range(num_stages):
        start = stage * 5
        stage_values = flat_coeffs[start : start + 5]

        formatted = ", ".join(format_float32_for_c(value) for value in stage_values)

        coefficient_lines.append(
            f"    {formatted}  /* stage {stage + 1}: b0, b1, b2, a1, a2 */"
        )

    coefficient_text = ",\n".join(coefficient_lines)

    header = f"""\
#ifndef DC_BLOCKER_BIQUAD_H
#define DC_BLOCKER_BIQUAD_H

#include "arm_math.h"

#define DC_BLOCKER_FS_HZ          ({fs_hz:.1f}f)
#define DC_BLOCKER_FC_HZ          ({fc_hz:.1f}f)
#define DC_BLOCKER_FILTER_ORDER   ({filter_order}U)
#define DC_BLOCKER_NUM_STAGES     ({num_stages}U)

/*
 * CMSIS-DSP arm_biquad_cascade_df2T_f32系数格式：
 *
 * {{b0, b1, b2, a1, a2}}
 *
 * 注意：
 * 这里的a1和a2已经由SciPy符号转换为CMSIS-DSP符号，
 * STM32端不得再次取反。
 */
static const float32_t dc_blocker_coeffs[
    5U * DC_BLOCKER_NUM_STAGES
] = {{
{coefficient_text}
}};

/*
 * DF2T每个二阶节需要两个状态变量：
 * {{d11, d12, d21, d22, ...}}
 *
 * 该数组不能声明为const。
 */
static float32_t dc_blocker_state[
    2U * DC_BLOCKER_NUM_STAGES
] = {{0.0f}};

#endif
"""

    output_path.write_text(header, encoding="utf-8")

    print(f"\n已生成头文件：{output_path.resolve()}")


# ============================================================
# 绘制响应
# ============================================================


def plot_response(
    sos: np.ndarray,
    fs_hz: float,
    fc_hz: float,
    output_path: Path,
) -> None:
    frequency_points = np.linspace(
        0.0,
        fs_hz / 2.0,
        65536,
        endpoint=True,
    )

    frequencies, response = calculate_frequency_response(
        sos,
        fs_hz,
        frequency_points,
    )

    magnitude_db = 20.0 * np.log10(
        np.maximum(
            np.abs(response),
            np.finfo(np.float64).tiny,
        )
    )

    figure, axes = plt.subplots(
        2,
        1,
        figsize=(10, 8),
        constrained_layout=True,
    )

    axes[0].plot(frequencies, magnitude_db)
    axes[0].axvline(
        fc_hz,
        color="red",
        linestyle="--",
        label=f"fc = {fc_hz / 1000.0:.1f} kHz",
    )
    axes[0].axvspan(
        190_000.0,
        210_000.0,
        color="green",
        alpha=0.15,
        label="190~210 kHz有效中频边带",
    )
    axes[0].set_xlim(0.0, fs_hz / 2.0)
    axes[0].set_ylim(-100.0, 3.0)
    axes[0].set_xlabel("频率/Hz")
    axes[0].set_ylabel("幅度/dB")
    axes[0].set_title("二阶Butterworth DC Blocker完整幅频响应")
    axes[0].grid(True)
    axes[0].legend()

    axes[1].plot(frequencies, magnitude_db)
    axes[1].axvline(
        fc_hz,
        color="red",
        linestyle="--",
    )
    axes[1].set_xlim(0.0, 30_000.0)
    axes[1].set_ylim(-100.0, 3.0)
    axes[1].set_xlabel("频率/Hz")
    axes[1].set_ylabel("幅度/dB")
    axes[1].set_title("DC及低频区域")
    axes[1].grid(True)

    figure.savefig(output_path, dpi=160)
    plt.close(figure)

    print(f"已生成频率响应图：{output_path.resolve()}")


# ============================================================
# 主程序
# ============================================================


def main() -> None:
    if not 0.0 < FC_HZ < FS_HZ / 2.0:
        raise ValueError("截止频率必须位于0和奈奎斯特频率之间")

    if FILTER_ORDER != 2:
        raise ValueError("当前设计要求二阶滤波器，FILTER_ORDER应为2")

    # 使用SOS输出，避免先生成高阶b/a多项式再进行分解。
    scipy_sos_float64 = signal.butter(
        N=FILTER_ORDER,
        Wn=FC_HZ,
        btype="highpass",
        fs=FS_HZ,
        output="sos",
    )

    print("SciPy SOS系数，float64：")
    print("[b0, b1, b2, a0, a1, a2]")
    print(scipy_sos_float64)

    # 转换为STM32实际使用的CMSIS float32系数。
    cmsis_coeffs_float32 = scipy_sos_to_cmsis_df2t(scipy_sos_float64)

    print("\nCMSIS-DSP DF2T系数，float32：")
    print("[b0, b1, b2, a1, a2]")
    print(cmsis_coeffs_float32)

    # 用量化后的CMSIS系数重新构造SciPy SOS。
    scipy_sos_float32 = cmsis_df2t_to_scipy_sos(cmsis_coeffs_float32)

    check_float32_stability(scipy_sos_float32)

    print_key_frequency_response(
        scipy_sos_float32,
        FS_HZ,
    )

    # --------------------------------------------------------
    # 验证CMSIS系数转换符号和排列
    # --------------------------------------------------------

    rng = np.random.default_rng(seed=2022)

    test_input = rng.standard_normal(BLOCK_SIZE).astype(np.float32)

    scipy_output = signal.sosfilt(
        scipy_sos_float32,
        test_input,
    ).astype(np.float32)

    cmsis_reference_output = cmsis_df2t_reference(
        test_input,
        cmsis_coeffs_float32,
    )

    maximum_error = np.max(
        np.abs(
            scipy_output.astype(np.float64) - cmsis_reference_output.astype(np.float64)
        )
    )

    print(f"\nSciPy SOS与CMSIS DF2T参考模型的最大绝对误差：{maximum_error:.9e}")

    # 两者内部运算次序不同，float32下不要求逐位完全相同。
    if maximum_error > 1.0e-4:
        raise RuntimeError("CMSIS转换验证失败，请检查反馈系数符号或排列")

    generate_cmsis_header(
        cmsis_coeffs_float32,
        FS_HZ,
        FC_HZ,
        FILTER_ORDER,
        OUTPUT_HEADER,
    )

    plot_response(
        scipy_sos_float32,
        FS_HZ,
        FC_HZ,
        OUTPUT_FIGURE,
    )


if __name__ == "__main__":
    main()
