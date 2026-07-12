/**
 * @file    dsp_filter.c
 * @brief   数字滤波器实现 + 系数库
 *
 * 系数维护规则（与 MATLAB 设计脚本联动）：
 *   1. 每个滤波器在 .h 中声明：系数指针、长度、采样率、设计 spec
 *   2. 每个滤波器在 .c 中定义 const 系数数组
 *   3. 修改系数时必须同步更新注释里的 MATLAB 脚本/参数
 *   4. state 缓冲区长度约定：
 *      - FIR:  num_taps + block_size - 1
 *      - Biquad: 2 * num_stages
 *
 * 从 MATLAB 导出系数的标准流程：
 *   1) 运行设计脚本（fir1 / butter / cheby1 等）得到系数向量
 *   2) 用 sprintf('%.10ff,', coeffs) 一次性导出为 C 数组初始化串
 *   3) 粘贴到下方对应的 const 数组中
 *   4) 同步修改 .h 中的 NUM_TAPS / NUM_STAGES 宏
 */

#include "dsp_filter.h"

/*===========================================================================
 * FIR 系数
 *===========================================================================*/

/* 5 点滑动平均：h = ones(1,5)/5
 * 本质是矩形窗截断的理想低通（sinc），系数全相等且和为 1
 * 频响在 DC 增益为 1，无相位失真（系数对称 → 线性相位）
 */
const float32_t dsp_flt_fir_ma5_coeffs[DSP_FLT_FIR_MA5_TAPS] = {
    0.2f, 0.2f, 0.2f, 0.2f, 0.2f
};

/* FIR 占位模板：默认单位冲激响应（全通 = 不做滤波）
 * 等待用户从 MATLAB 粘贴自定义 FIR 系数
 *
 * MATLAB 参考脚本：
 *   Fs = 1024000;             // 采样率 (Hz)
 *   N  = 64;                  // 阶数（系数个数 = N+1）
 *   fc = 100000;              // 截止频率 (Hz)
 *   h  = fir1(N, fc/(Fs/2));  // Hamming 窗默认
 *   sprintf('%.10ff,', h)     // 导出 C 数组
 */
const float32_t dsp_flt_fir_template_coeffs[DSP_FLT_FIR_TEMPLATE_TAPS] = {
    /* === 在此粘贴 MATLAB fir1/firpm 输出 === */
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f
    /* ====================================== */
};

/*===========================================================================
 * IIR Biquad 系数
 *===========================================================================*/

/* 2 阶 Butterworth 低通，fc=2 kHz（针对 Fs=8 kHz 设计，移植时注意 Fs）
 *
 * MATLAB 等价脚本：
 *   Fs = 8000;
 *   fc = 2000;
 *   [b, a]   = butter(2, fc/(Fs/2));
 *   [sos, g] = tf2sos(b, a);
 *   % CMSIS 系数布局：每段 {b0,b1,b2,a1,a2}，a1/a2 取负
 *   sos_cmsis = [sos(:,1)*g, sos(:,2)*g, sos(:,3)*g, -sos(:,5), -sos(:,6)];
 *   sprintf('%.10ff,', sos_cmsis')   % 按行导出
 *
 * 手工推导（双线性变换 K = tan(π*fc/Fs) = tan(π/4) = 1）：
 *   b0 = K²/(1+√2*K+K²)       ≈ 0.2928932188
 *   b1 = 2*b0                  ≈ 0.5857864376
 *   b2 = b0                    ≈ 0.2928932188
 *   a1_matlab = 2*(K²-1)/denom = 0.0
 *   a2_matlab = (1-√2*K+K²)/denom ≈ 0.1715728753
 *   CMSIS a1 = -a1_matlab = 0.0
 *   CMSIS a2 = -a2_matlab = -0.1715728753
 */
const float32_t dsp_flt_biquad_lp_butter2_2khz_coeffs[5U * DSP_FLT_BIQUAD_LP_BUTTER2_2KHZ_STAGES] = {
    /* stage 0: {b0, b1, b2, a1, a2} */
    0.2928932188f, 0.5857864376f, 0.2928932188f, 0.0f, -0.1715728753f
};

/* Biquad 占位模板：默认全通（每段单位响应）
 * 等待用户从 MATLAB 粘贴自定义 IIR 系数
 *
 * MATLAB 参考脚本：
 *   Fs = 1024000;
 *   fc = 50000;               // 截止频率 (Hz)
 *   [b, a] = butter(2, fc/(Fs/2));       // 或 cheby1/ellip
 *   [sos, g] = tf2sos(b, a);
 *   % 增益 g 乘入第一段 b0/b1/b2
 *   sos(1,1:3) = sos(1,1:3) * g;
 *   % CMSIS 顺序：{b0,b1,b2,-a1,-a2}，a1=a2 取 s 平面极点实部负值
 *   cmsis_coeffs = [sos(:,1:3), -sos(:,5:6)];
 *   sprintf('%.10ff,', cmsis_coeffs')   % 导出
 */
const float32_t dsp_flt_biquad_template_coeffs[5U * DSP_FLT_BIQUAD_TEMPLATE_STAGES] = {
    /* === 在此粘贴 MATLAB tf2sos 输出 === */
    /* stage 0 */ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    /* stage 1 */ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f
    /* ================================== */
};

/*===========================================================================
 * FIR 实现（薄包装 arm_fir_f32）
 *===========================================================================*/

/**
 * @brief  初始化 FIR 滤波器
 *
 * 内部调用 arm_fir_init_f32，将系数/状态指针注册到 CMSIS-DSP 实例中。
 * CMSIS-DSP 的 arm_fir_f32 内部维护状态缓冲用于跨块卷积衔接，
 * 调用方无需关心跨块状态——连续调用 Process 即可。
 */
int DspFilter_FIR_Init(DspFilter_FIR  *flt,
                       uint16_t        num_taps,
                       const float32_t *coeffs,
                       float32_t       *state,
                       uint32_t        block_size)
{
    if ((flt == NULL) || (coeffs == NULL) || (state == NULL) ||
        (num_taps == 0U) || (block_size == 0U)) {
        return -1;
    }
    arm_fir_init_f32(&flt->inst, num_taps,
                     (float32_t *)coeffs, state, block_size);
    return 0;
}

/**
 * @brief  执行 FIR 滤波
 *
 * 每次处理 block_size 个样本，输出与输入等长。
 * 支持 in-place（src == dst），但不推荐——CMSIS-DSP 内部实现
 * 对 in-place 的支持取决于具体版本。
 */
void DspFilter_FIR_Process(DspFilter_FIR   *flt,
                           const float32_t *src,
                           float32_t       *dst,
                           uint32_t         block_size)
{
    if ((flt == NULL) || (src == NULL) || (dst == NULL) || (block_size == 0U)) {
        return;
    }
    arm_fir_f32(&flt->inst, (float32_t *)src, dst, block_size);
}

/*===========================================================================
 * IIR Biquad 实现（薄包装 arm_biquad_cascade_df2T_f32）
 *===========================================================================*/

/**
 * @brief  初始化 Biquad 级联滤波器
 *
 * DF2T（Direct Form II Transposed）结构的优点：
 *   - 每段只需 2 个状态变量（vs 直接 I 型的 4 个）
 *   - 数值精度比直接型好（舍入误差不跨段累积）
 *   - 适合定点实现（本项目用浮点，主要享受简洁性）
 */
int DspFilter_Biquad_Init(DspFilter_Biquad *flt,
                          uint8_t           num_stages,
                          const float32_t  *coeffs,
                          float32_t        *state)
{
    if ((flt == NULL) || (coeffs == NULL) || (state == NULL) ||
        (num_stages == 0U)) {
        return -1;
    }
    arm_biquad_cascade_df2T_init_f32(&flt->inst, num_stages,
                                     (float32_t *)coeffs, state);
    return 0;
}

/**
 * @brief  执行 Biquad 滤波
 *
 * 信号依次经过每一段 biquad（级联），每段的输出是下一段的输入。
 * 支持 in-place（src == dst）。
 */
void DspFilter_Biquad_Process(DspFilter_Biquad *flt,
                              const float32_t  *src,
                              float32_t        *dst,
                              uint32_t          block_size)
{
    if ((flt == NULL) || (src == NULL) || (dst == NULL) || (block_size == 0U)) {
        return;
    }
    arm_biquad_cascade_df2T_f32(&flt->inst, (float32_t *)src, dst, block_size);
}
