/**
 * @file    dsp_filter.h
 * @brief   数字滤波器模块 —— FIR / IIR Biquad 的 CMSIS-DSP 薄包装
 *
 * 设计原则：
 *   - 本模块是对 CMSIS-DSP FilteringFunctions 的轻量封装，不重复造轮子
 *   - 所有 state 缓冲由调用方提供（CMSIS-DSP 内部只保存指针），
 *     使用完毕后可自行释放或复用
 *   - 滤波器系数来自 MATLAB 设计脚本，遵循 dsp_coeffs.c 的维护约定
 *
 * 依赖：
 *   - CMSIS-DSP (arm_math.h)：arm_fir_f32 / arm_biquad_cascade_df2T_f32
 *   - app_config.h：DSP_FS_HZ 采样率基准
 *
 * 典型用法：
 *   @code
 *   // ---- FIR ----
 *   DspFilter_FIR flt;
 *   float state[NUM_TAPS + BLOCK_SIZE - 1];
 *   DspFilter_FIR_Init(&flt, NUM_TAPS, coeffs, state, BLOCK_SIZE);
 *   DspFilter_FIR_Process(&flt, src, dst, BLOCK_SIZE);
 *
 *   // ---- Biquad ----
 *   DspFilter_Biquad biq;
 *   float biq_state[2 * NUM_STAGES];
 *   DspFilter_Biquad_Init(&biq, NUM_STAGES, coeffs, biq_state);
 *   DspFilter_Biquad_Process(&biq, src, dst, BLOCK_SIZE);
 *   @endcode
 */

#ifndef __DSP_FILTER_H
#define __DSP_FILTER_H

#include "arm_math.h"

/*===========================================================================
 * 一、FIR 滤波器（直接型，浮点）
 *
 *   差分方程：y[n] = Σ b[k] * x[n-k] ，k = 0..num_taps-1
 *   适用场景：线性相位滤波（信号波形保真度高），如抗混叠、去噪
 *===========================================================================*/

/** FIR 滤波器实例（封装 CMSIS-DSP arm_fir_instance_f32） */
typedef struct {
    arm_fir_instance_f32 inst;   /* CMSIS 内部句柄，不要手动读写 */
} DspFilter_FIR;

/**
 * @brief  初始化 FIR 滤波器
 * @param  flt        滤波器实例指针
 * @param  num_taps   抽头数（系数个数），即差分方程阶数 + 1
 * @param  coeffs     系数数组 [num_taps]，由 MATLAB fir1/firpm 设计导出
 * @param  state      状态缓冲，长度 >= num_taps + block_size - 1
 * @param  block_size 每次处理的样本数
 * @return 0=成功, -1=参数非法
 */
int  DspFilter_FIR_Init(DspFilter_FIR  *flt,
                        uint16_t        num_taps,
                        const float32_t *coeffs,
                        float32_t       *state,
                        uint32_t        block_size);

/**
 * @brief  执行 FIR 滤波（逐块处理）
 * @param  flt        已初始化的滤波器实例
 * @param  src        输入样本 [block_size]
 * @param  dst        输出样本 [block_size]（可与 src 不同 buffer）
 * @param  block_size 本次处理的样本数（必须与 Init 时一致）
 *
 * 算法复杂度：O(num_taps * block_size)
 * 对于实时流处理：连续调用即可，state 会自动维护跨块状态
 */
void DspFilter_FIR_Process(DspFilter_FIR   *flt,
                           const float32_t *src,
                           float32_t       *dst,
                           uint32_t         block_size);

/*===========================================================================
 * 二、IIR Biquad 级联滤波器（DF2T 转置直接 II 型，浮点）
 *
 *   每段 biquad 的差分方程（CMSIS 约定，a1/a2 已取负号）：
 *     y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 *
 *   适用场景：陡峭截止滤波（阶数低、计算量小），如工频陷波、带通选频
 *   与 FIR 的区别：IIR 非线性相位（对波形保真度有要求时慎用），
 *   但同等衰减所需阶数远低于 FIR
 *
 *   MATLAB 设计流程：
 *     [b,a] = butter(2, fc/(Fs/2));     // 设计模拟原型 → 双线性变换
 *     [sos,g] = tf2sos(b,a);            // 拆成二阶节（数值更稳定）
 *     对每段 sos_i: CMSIS = [b0*g_i, b1*g_i, b2*g_i, -a1, -a2]
 *===========================================================================*/

/** Biquad 滤波器实例（封装 CMSIS-DSP arm_biquad_cascade_df2T_instance_f32） */
typedef struct {
    arm_biquad_cascade_df2T_instance_f32 inst;  /* CMSIS 内部句柄 */
} DspFilter_Biquad;

/**
 * @brief  初始化 Biquad 级联滤波器
 * @param  flt         滤波器实例指针
 * @param  num_stages  级联段数（每段是一个二阶节）
 * @param  coeffs      系数数组 [5 * num_stages]
 *                     布局: {b0,b1,b2,a1,a2} 每段 5 个，a1/a2 已取负
 * @param  state       状态缓冲，长度 >= 2 * num_stages
 * @return 0=成功, -1=参数非法
 */
int  DspFilter_Biquad_Init(DspFilter_Biquad *flt,
                           uint8_t           num_stages,
                           const float32_t  *coeffs,
                           float32_t        *state);

/**
 * @brief  执行 Biquad 滤波（逐块处理）
 * @param  flt        已初始化的滤波器实例
 * @param  src        输入样本 [block_size]
 * @param  dst        输出样本 [block_size]
 * @param  block_size 本次处理的样本数
 *
 * 算法复杂度：O(num_stages * block_size)
 * 对于高阶 IIR：串联更多 biquad 段即可，数值稳定性优于直接型
 */
void DspFilter_Biquad_Process(DspFilter_Biquad *flt,
                              const float32_t  *src,
                              float32_t        *dst,
                              uint32_t          block_size);

/*===========================================================================
 * 三、内置滤波器系数预设
 *
 *   命名规则：DSP_FLT_<类型>_<特征>_<参数>
 *   所有系数来自 MATLAB 设计 → 详见 dsp_filter.c 中各系数的注释
 *===========================================================================*/

/* ---- FIR: 5 点滑动平均（低通，最简验证模板）----
 * 差分方程: y[n] = 0.2 * (x[n] + x[n-1] + ... + x[n-4])
 * 频响: |H(f)| = |sin(5πf/Fs) / (5*sin(πf/Fs))|
 * 在 Fs=1.024MHz 处: 3dB 截止频率 ≈ 0.13*Fs/2 ≈ 66 kHz
 */
#define DSP_FLT_FIR_MA5_TAPS  5U
extern const float32_t dsp_flt_fir_ma5_coeffs[DSP_FLT_FIR_MA5_TAPS];

/* ---- FIR: 占位模板（粘贴 MATLAB fir1/firpm 输出）----
 * 默认初始化为单位冲激响应（全通）
 * 使用步骤见 dsp_filter.c 顶部注释
 */
#define DSP_FLT_FIR_TEMPLATE_TAPS  8U
extern const float32_t dsp_flt_fir_template_coeffs[DSP_FLT_FIR_TEMPLATE_TAPS];

/* ---- IIR Biquad: 2 阶 Butterworth 低通 fc=2kHz ----
 * MATLAB: [b,a]=butter(2,2000/(Fs/2)); [sos,g]=tf2sos(b,a)
 * CMSIS: a1/a2 已取负，增益 g 乘入第一段 b0/b1/b2
 * 通带: 0 ~ 2000 Hz（几乎无衰减）
 */
#define DSP_FLT_BIQUAD_LP_BUTTER2_2KHZ_STAGES  1U
extern const float32_t dsp_flt_biquad_lp_butter2_2khz_coeffs[5U * DSP_FLT_BIQUAD_LP_BUTTER2_2KHZ_STAGES];

/* ---- IIR Biquad: 占位模板（粘贴 MATLAB butter+tf2sos 输出）----
 * 默认初始化为全通（每段单位响应）
 * 使用步骤见 dsp_filter.c 顶部注释
 */
#define DSP_FLT_BIQUAD_TEMPLATE_STAGES  2U
extern const float32_t dsp_flt_biquad_template_coeffs[5U * DSP_FLT_BIQUAD_TEMPLATE_STAGES];

#endif /* __DSP_FILTER_H */
