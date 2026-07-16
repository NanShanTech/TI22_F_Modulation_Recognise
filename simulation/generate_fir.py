import numpy as np
from scipy import signal


FS = 1.024e6
F_PASS = 100e3
F_STOP = 250e3
F_CUTOFF = 175e3
ATTENUATION_DB = 60.0

# CMSIS-DSP每次处理4096个采样点
BLOCK_SIZE = 4096


def design_and_export() -> np.ndarray:
    normalized_width = (F_STOP - F_PASS) / (FS / 2.0)

    num_taps, beta = signal.kaiserord(
        ripple=ATTENUATION_DB,
        width=normalized_width,
    )

    scipy_coeffs = signal.firwin(
        numtaps=num_taps,
        cutoff=F_CUTOFF,
        window=("kaiser", beta),
        pass_zero=True,
        fs=FS,
        scale=True,
    )

    # CMSIS-DSP要求按时间反序存放
    cmsis_coeffs = scipy_coeffs[::-1].astype(np.float32)

    # 使用高密度频率网格检验
    frequency, response = signal.freqz(
        scipy_coeffs,
        worN=1_048_576,
        fs=FS,
    )

    magnitude = np.abs(response)

    passband = magnitude[frequency <= F_PASS]
    stopband = magnitude[frequency >= F_STOP]

    passband_min = np.min(passband)
    passband_max = np.max(passband)
    passband_ripple_db = 20.0 * np.log10(passband_max / passband_min)

    stopband_max = np.max(stopband)
    stopband_attenuation_db = -20.0 * np.log10(stopband_max)

    fp_index = np.argmin(np.abs(frequency - F_PASS))
    fst_index = np.argmin(np.abs(frequency - F_STOP))

    fp_gain_db = 20.0 * np.log10(magnitude[fp_index])
    fst_gain_db = 20.0 * np.log10(magnitude[fst_index])

    group_delay_samples = (num_taps - 1) / 2.0
    group_delay_us = group_delay_samples / FS * 1e6

    print(f"normalized_width       = {normalized_width:.10f}")
    print(f"num_taps               = {num_taps}")
    print(f"FIR order              = {num_taps - 1}")
    print(f"beta                    = {beta:.10f}")
    print(f"DC gain                 = {np.sum(scipy_coeffs):.10f}")
    print(f"passband ripple         = {passband_ripple_db:.6f} dB")
    print(f"gain at 100 kHz         = {fp_gain_db:.6f} dB")
    print(f"gain at 250 kHz         = {fst_gain_db:.6f} dB")
    print(f"worst stop attenuation  = {stopband_attenuation_db:.6f} dB")
    print(f"group delay             = {group_delay_samples:.1f} samples")
    print(f"group delay             = {group_delay_us:.6f} us")

    if stopband_attenuation_db < ATTENUATION_DB:
        raise RuntimeError("实际阻带衰减未达到指定值，请增加抽头数或设计余量")

    print("\nstatic const float32_t firCoeffs[NUM_TAPS] =")
    print("{")
    for index, coefficient in enumerate(cmsis_coeffs):
        separator = "," if index < len(cmsis_coeffs) - 1 else ""
        print(f"    {coefficient:.10e}f{separator}")
    print("};")

    return cmsis_coeffs


if __name__ == "__main__":
    design_and_export()
