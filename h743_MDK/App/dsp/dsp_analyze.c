/**
 * @file    dsp_analyze.c
 * @brief   增强频谱分析实现
 *
 * RFFT 路径比 CFFT 更适合 ADC 纯实数数据：
 *   实数输入 → arm_rfft_fast_f32 → 复数频谱（Hermitian 对称）
 *   → arm_cmplx_mag_f32 → 幅度谱（只需前 N/2 个 bin）
 *
 * 与 fft_analyzer 中 CFFT 路径的关系：
 *   CFFT: N 个复数输入（实部=ADC, 虚部=0）→ N 个复数输出 → N 个幅度
 *         内存: 2N float 输入 + N float 幅度，运算约 2*N*log2(2N)
 *   RFFT: N 个实数输入 → N/2 个有效复数输出 → N/2 个幅度
 *         内存: N float 输入 + N float scratch + N/2 float 幅度 ≈ 2.5N
 *         运算约 N/2*log2(N)，约为 CFFT 的一半
 *
 * 注意：RFFT 的 bin 0 存放 DC 分量和 Nyquist 频率的打包值，
 *   这两个值没有独立的幅度意义，本实现自动置零。
 */

#include "dsp_analyze.h"
#include <math.h>

/*===========================================================================
 * RFFT 实例初始化
 *===========================================================================*/

/**
 * @brief  初始化频谱分析上下文
 *
 * 仅初始化用户指定大小的 RFFT 实例，避免预初始化所有大小浪费内存。
 * arm_rfft_fast_init_f32 内部会计算旋转因子表。
 */
int DspAnalyze_Init(DspAnalyze *ctx, uint32_t fft_size)
{
    if (ctx == NULL) {
        return -1;
    }

    /* CMSIS-DSP arm_rfft_fast_f32 支持的合法点数 */
    switch (fft_size) {
    case 128U:
    case 256U:
    case 512U:
    case 1024U:
    case 2048U:
    case 4096U:
        break;
    default:
        return -1;  /* 不支持的点数 */
    }

    arm_rfft_fast_init_f32(&ctx->rfft_inst, (uint16_t)fft_size);
    ctx->fft_size = fft_size;
    ctx->inited   = 1U;
    return 0;
}

/**
 * @brief  实数 FFT 取幅度谱
 *
 * 步骤说明：
 *   1) arm_rfft_fast_f32:
 *      输入: in[N] 实数数组
 *      输出: scratch[N] 复数频谱（实部/虚部交错存放）
 *      内部使用 Cooley-Tukey 基-2 DIT 算法，利用 Hermitian 对称省去一半蝴蝶运算
 *
 *   2) arm_cmplx_mag_f32:
 *      输入: scratch[N] 复数对
 *      输出: mag[N/2] 幅度值 = sqrt(real² + imag²)
 *
 *   3) mag[0] = 0:
 *      RFFT 的 bin 0 特殊：存放 X[0]（DC）和 X[N/2]（Nyquist）的打包格式：
 *        scratch[0] = X[0]（DC）
 *        scratch[1] = X[N/2]（Nyquist）
 *      这两个值的模没有实际的"单一频率幅度"含义，置零避免干扰峰值搜索。
 */
int DspAnalyze_RFFT_Mag(DspAnalyze     *ctx,
                        const float32_t *in,
                        float32_t       *scratch,
                        float32_t       *mag)
{
    if ((ctx == NULL) || (ctx->inited == 0U) ||
        (in == NULL) || (scratch == NULL) || (mag == NULL)) {
        return -1;
    }

    const uint32_t N = ctx->fft_size;

    /* RFFT: 实数 in → 复数频谱 scratch */
    arm_rfft_fast_f32(&ctx->rfft_inst,
                      (float32_t *)in,
                      scratch,
                      0);  /* 0=正变换, 1=逆变换 */

    /* 复数频谱 → 幅度谱（只取前 N/2 个 bin） */
    arm_cmplx_mag_f32(scratch, mag, N / 2U);

    /* bin 0 置零：避免 DC/Nyquist 打包值干扰峰值搜索 */
    mag[0] = 0.0f;

    return 0;
}

/**
 * @brief  查找幅度谱峰值
 *
 * 直接调用 CMSIS-DSP arm_max_f32。
 * bin 0 已被 RFFT_Mag 置零，无需额外处理。
 */
void DspAnalyze_FindPeak(const float32_t *mag, uint32_t len,
                         uint32_t *peak_bin, float32_t *peak_val)
{
    if ((mag == NULL) || (peak_bin == NULL) || (peak_val == NULL)) {
        return;
    }
    arm_max_f32(mag, len, peak_val, peak_bin);
}

/*===========================================================================
 * 峰值频率内插
 *===========================================================================*/

/**
 * @brief  峰值频率抛物线内插
 *
 * 数学原理：
 *   设峰值点的三个相邻幅度值为 L（左）、P（中）、R（右），
 *   以 bin 坐标为横轴、log-magnitude 为纵轴（或直接用 linear magnitude），
 *   过三点拟合抛物线 y = a*x² + b*x + c。
 *
 *   抛物线顶点位置: delta = -b/(2a) = (L - R) / (2*(L - 2P + R))
 *
 *   推导（离散索引 x = -1, 0, +1 对应 L, P, R）：
 *     L = a*(-1)² + b*(-1) + c = a - b + c
 *     P = a*0 + b*0 + c = c
 *     R = a*1² + b*1 + c = a + b + c
 *     解得: a = (L+R)/2 - P,  b = (R-L)/2
 *     delta = -b/(2a) = (L-R) / (2*(L - 2P + R))
 *
 *   插值后的精确频率: f = (peak_bin + delta) * Fs / N
 *
 * 实际精度限制：
 *   - 纯正弦 + 矩形窗：delta 误差 < 0.01 bin（主瓣形状接近 sinc 而非抛物线）
 *   - 纯正弦 + Hann 窗：delta 误差 < 0.05 bin（窗函数展宽了主瓣）
 *   - 信噪比 > 40 dB 时，重复精度通常 < 0.02 bin
 *   - 存在邻近干扰频率时，内插结果不可靠（bin 混叠导致幅度包络变形）
 *
 * 边界处理：
 *   peak_bin 在 0 或 len-1 时无左/右邻居，直接返回离散 bin 频率。
 *   实际场景中 DC 和高频 bin 通常不会被选为峰值（已置零或超出信号范围）。
 */
float DspAnalyze_InterpFreq(const float32_t *mag, uint32_t len,
                            uint32_t peak_bin,
                            float    fs_hz, uint32_t fft_size)
{
    if ((mag == NULL) || (len < 3U) || (fft_size == 0U)) {
        return 0.0f;
    }

    /* 边界：无法内插时返回离散 bin 频率 */
    if ((peak_bin == 0U) || (peak_bin >= len - 1U)) {
        return (float)peak_bin * fs_hz / (float)fft_size;
    }

    const float L = mag[peak_bin - 1U];  /* 左邻居幅度 */
    const float P = mag[peak_bin];       /* 峰值幅度 */
    const float R = mag[peak_bin + 1U];  /* 右邻居幅度 */

    /* 分母 = L - 2P + R：对于纯噪声 flat 区域 → 接近 0，
       此时内插无意义，回退到离散 bin */
    const float denom = L - 2.0f * P + R;
    if (fabsf(denom) < 1e-9f) {
        return (float)peak_bin * fs_hz / (float)fft_size;
    }

    /* delta = 0.5 * (L - R) / (L - 2P + R) */
    const float delta = 0.5f * (L - R) / denom;

    /* delta 范围应落在 [-0.5, 0.5]，
       超出意味着峰值不在当前 bin（可能是邻近 bin 更高的"鞍点"） */
    float clamped = delta;
    if (clamped > 0.5f)  clamped = 0.5f;
    if (clamped < -0.5f) clamped = -0.5f;

    const float interp_bin = (float)peak_bin + clamped;
    return interp_bin * fs_hz / (float)fft_size;
}

/*===========================================================================
 * RMS 有效值
 *===========================================================================*/

/**
 * @brief  计算信号 RMS 有效值
 *
 * 薄包装 arm_rms_f32。
 *
 * RMS 与 Vpp 的换算（已知波形类型时）：
 *   正弦: Vpp = RMS * 2√2 ≈ RMS * 2.828
 *   注意：该关系仅在信号不含 DC 偏移时成立；
 *   若有 DC 偏移（如 Vref=1.65V），需先去直流再算 Vpp。
 */
float DspAnalyze_RMS(const float32_t *x, uint32_t n)
{
    if ((x == NULL) || (n == 0U)) {
        return 0.0f;
    }
    float32_t rms;
    arm_rms_f32((float32_t *)x, n, &rms);
    return rms;
}

/*===========================================================================
 * SNR 信噪比估算（频域法）
 *===========================================================================*/

/**
 * @brief  频域 SNR 估算
 *
 * 算法详解：
 *   - 信号区域：peak_bin 及其紧邻（±guard 范围内的 bin 视为信号泄露）
 *   - 噪声区域：信号区域以外的所有 bin
 *   - 信号功率 = mag[peak_bin]²
 *   - 噪声功率 = 噪声区域各 bin 幅度的均方值（mean of squared magnitudes）
 *   - SNR_dB = 10 * log10(Ps / Pn)
 *
 * 简化假设：
 *   假设噪声是白噪声（功率谱平坦），且信号 bin 不含显著噪声。
 *   这个假设在电赛"已知信号频率范围"的场景下基本合理。
 *
 * 局限性：
 *   - 有色噪声（如 1/f 噪声）集中在低频段，频域估计会偏低
 *   - 多频率同时存在时（多音信号），需分别保护各自的信号 bin
 *   - 低 SNR（< 10 dB）时，噪声 bin 的统计波动大，估计方差大
 */
float DspAnalyze_SNR(const float32_t *mag, uint32_t len,
                     uint32_t peak_bin, uint32_t guard)
{
    if ((mag == NULL) || (len == 0U) || (peak_bin >= len)) {
        return 0.0f;
    }

    /* 信号功率 */
    const float sig_pow = mag[peak_bin] * mag[peak_bin];
    if (sig_pow < 1e-12f) {
        return 0.0f;  /* 无信号可测 */
    }

    /* 噪声区域：排除 [peak_bin-guard, peak_bin+guard] */
    float  noise_sum = 0.0f;
    uint32_t noise_cnt = 0U;
    const uint32_t lo = (peak_bin > guard) ? (peak_bin - guard) : 0U;
    const uint32_t hi = (peak_bin + guard < len) ? (peak_bin + guard) : (len - 1U);

    /* bin 0 强制计入噪声（DC/Nyquist 不是信号） */
    noise_sum += mag[0] * mag[0];
    noise_cnt++;

    for (uint32_t i = 1U; i < len; i++) {
        /* 跳过 bin 0（已处理）和信号保护区 */
        if (i >= lo && i <= hi) {
            continue;
        }
        noise_sum += mag[i] * mag[i];
        noise_cnt++;
    }

    if (noise_cnt == 0U) {
        return 100.0f;  /* 几乎所有 bin 都被信号占据，SNR 极高 */
    }

    const float noise_pow = noise_sum / (float)noise_cnt;

    /* SNR = 10 * log10(Ps/Pn)，加保护值防止除零 */
    if (noise_pow < 1e-15f) {
        return 100.0f;
    }

    return 10.0f * log10f(sig_pow / noise_pow);
}

/*===========================================================================
 * THD 总谐波失真
 *===========================================================================*/

/**
 * @brief  频域 THD 计算
 *
 * 算法详解：
 *   各谐波频率 f_h = h * f_fund（h=2,3,...,max_harm）
 *   对应的 bin 位置: harm_bin_h ≈ h * peak_bin
 *   在 ±1 bin 范围内搜索该谐波的最大幅度（补偿 FFT bin 分辨率误差）
 *
 * THD 的两种常用表示：
 *   THD_ratio = sqrt(Σ|X(h*f0)|²) / |X(f0)|     ← 本函数返回此值
 *   THD_pct   = THD_ratio * 100%
 *   THD_dB    = 20 * log10(THD_ratio)
 *
 * 谐波来源的物理含义：
 *   - 偶次谐波（2nd, 4th...）：信号波形不对称（如单管放大）
 *   - 奇次谐波（3rd, 5th...）：信号对称但非线性（如推挽交越失真）
 *   - THD 越大 → 失真越严重 → 信号"不纯"
 *
 * 注意事项：
 *   - 高次谐波幅度可能低于噪声 floor → DFT 检测不到 → 计为零
 *   - harmonic bin 超出频谱范围时跳过（如 5*fund > Fs/2 时 Nyquist 限制）
 *   - 使用 Hann/Hamming 窗时，相邻谐波的主瓣可能部分重叠，
 *     如果基频很低（bin 间距 < 5），THD 估计会偏高
 */
float DspAnalyze_THD(const float32_t *mag, uint32_t len,
                     uint32_t peak_bin, uint32_t max_harm,
                     uint32_t fft_size)
{
    if ((mag == NULL) || (len == 0U) || (peak_bin >= len) ||
        (peak_bin == 0U) || (fft_size == 0U)) {
        return 0.0f;
    }

    const float A_fund = mag[peak_bin];
    if (A_fund < 1e-9f) {
        return 0.0f;  /* 基波幅度为零 */
    }

    float harm_pow_sum = 0.0f;

    for (uint32_t h = 2U; h <= max_harm; h++) {
        /* 谐波频率 f_h = h * f_fund，对应 bin = h * peak_bin */
        uint32_t harm_bin = h * peak_bin;
        if (harm_bin >= len) {
            break;  /* 谐波超出 Nyquist 范围，更高效谐波也不再搜索 */
        }

        /* 在理论 bin 的 ±1 范围内找最大幅度——
           补偿因 f0 不恰好落在整数 bin 上导致的谐波偏移 */
        float harm_max = 0.0f;
        for (int32_t d = -1; d <= 1; d++) {
            int32_t idx = (int32_t)harm_bin + d;
            if (idx >= 0 && (uint32_t)idx < len) {
                if (mag[idx] > harm_max) {
                    harm_max = mag[idx];
                }
            }
        }

        harm_pow_sum += harm_max * harm_max;
    }

    /* THD = sqrt(谐波功率和) / 基波幅度 */
    return sqrtf(harm_pow_sum) / A_fund;
}
