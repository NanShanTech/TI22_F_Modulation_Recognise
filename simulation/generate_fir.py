import numpy as np
from scipy import signal

# ========== 你的原始滤波器设计（完全不变）==========
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
window_spec = ("kaiser", beta)
b = signal.firwin(
    numtaps=num_taps,
    cutoff=175e3,
    window=window_spec,
    pass_zero=True,
    fs=fs,
    scale=True,
)
print(f"num_taps = {num_taps}, beta = {beta}")

# ========== CMSIS-DSP 适配：反转 + Helium 补齐 ==========
# 1. 时间反序：CMSIS-DSP 要求系数为 {b[N-1], ..., b[1], b[0]}
b_reversed = b[::-1].copy().astype(np.float32)

# 2. Helium 补齐：f32 版本系数长度必须是 4 的整数倍，不足补 0
#    即使不用 Helium（Cortex-M0/M3/M4），补齐也不影响结果，统一加上更安全
pad_len = (4 - (num_taps % 4)) % 4
if pad_len > 0:
    b_padded = np.append(b_reversed, np.zeros(pad_len, dtype=np.float32))
else:
    b_padded = b_reversed

# 3. 导出为 C 数组字符串
coeff_str = ",\n    ".join(f"{coeff:.9f}f" for coeff in b_padded)
c_array = (
    f"/* FIR low-pass: fs=1.024MHz, cutoff=175kHz, num_taps={num_taps} */\n"
    f"#define FIR_NUM_TAPS  {num_taps}U\n"
    f"#define FIR_COEFF_LEN {len(b_padded)}U  /* padded to multiple of 4 for Helium */\n"
    f"static const float32_t fir_coeffs[FIR_COEFF_LEN] = {{\n"
    f"    {coeff_str}\n"
    f"}};"
)
print(c_array)
