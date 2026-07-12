/**
 * @file    dsp_adaptive.h
 * @brief   自适应滤波器模块 —— LMS / NLMS / RLS
 *
 * 自适应滤波器的核心思想：根据输入信号的统计特性，自动调整滤波器权值，
 * 使得输出信号与期望信号之间的均方误差（MSE）最小。
 *
 * 三种算法对比：
 * ┌──────────┬──────────────────────┬───────────────────┬──────────────┐
 * │  算法    │ 收敛速度             │ 计算复杂度(每样本) │ 稳态误差     │
 * ├──────────┼──────────────────────┼───────────────────┼──────────────┤
 * │  LMS     │ 慢（依赖特征值分布） │ O(N)              │ 有剩余误差   │
 * │  NLMS    │ 中等（归一化改善）   │ O(N)              │ 低于 LMS     │
 * │  RLS     │ 快（与特征值无关）   │ O(N²)             │ 最低         │
 * └──────────┴──────────────────────┴───────────────────┴──────────────┘
 *
 * 典型应用场景：
 *   - 自适应噪声抵消（ANC）：参考噪声 → 自适应滤波器 → 抵消主通道噪声
 *   - 系统辨识：已知输入/输出 → 自适应滤波器逼近未知系统传递函数
 *   - 信道均衡：消除码间干扰
 *   - 回声消除：语音通信中消除声学回声
 *
 * 依赖：
 *   - CMSIS-DSP (arm_math.h)：arm_lms_f32 / arm_lms_norm_f32 及矩阵运算
 *   - app_config.h：DSP_RLS_DELTA / DSP_RLS_LAMBDA / DSP_LMS_MU 默认参数
 */

#ifndef __DSP_ADAPTIVE_H
#define __DSP_ADAPTIVE_H

#include "arm_math.h"

/*===========================================================================
 * 一、LMS 自适应滤波器（Least Mean Square — 最小均方）
 *
 *   权值更新公式：w[n+1] = w[n] + μ * e[n] * x[n]
 *     w    : 滤波器权值向量 [taps]
 *     μ    : 步长（步长因子），控制收敛速度与稳态误差的折中
 *     e[n] : 瞬时误差 = d[n] - y[n]（期望 - 实际输出）
 *     x[n] : 输入信号向量（延迟线）
 *
 *   μ 选择指南：
 *     - 上限：0 < μ < 2/λ_max（λ_max 是输入自相关矩阵的最大特征值）
 *     - 实用经验：μ = 0.1/taps 为安全起点
 *     - 大 μ → 收敛快、稳态误差大；小 μ → 收敛慢、稳态误差小
 *     - 本项目 μ 默认值在 app_config.h 中定义
 *
 *   LMS 收敛条件：输入信号功率稳定时效果最好；功率波动大时考虑 NLMS
 *===========================================================================*/

/** LMS 滤波器实例（封装 CMSIS-DSP arm_lms_instance_f32） */
typedef struct {
    arm_lms_instance_f32 inst;   /* CMSIS 内部句柄 */
} DspAdaptive_LMS;

/**
 * @brief  初始化 LMS 自适应滤波器
 * @param  lms        滤波器实例指针
 * @param  num_taps   抽头数（权值个数），越大模型越精细但计算量越大
 * @param  coeffs     权值缓冲 [num_taps]，初始值通常清零（未知系统从零开始学习）
 * @param  state      状态缓冲 [num_taps + block_size - 1]
 * @param  mu         步长因子，建议 0.001~0.1 之间
 * @param  block_size 每次处理的样本数
 */
void DspAdaptive_LMS_Init(DspAdaptive_LMS *lms,
                          uint16_t         num_taps,
                          float32_t       *coeffs,
                          float32_t       *state,
                          float32_t        mu,
                          uint32_t         block_size);

/**
 * @brief  执行 LMS 自适应滤波（逐块处理）
 * @param  lms        已初始化的 LMS 实例
 * @param  src        输入信号 [block_size]（参考噪声或系统输入）
 * @param  ref        期望信号 [block_size]（d[n] = 期望输出）
 * @param  out        滤波器输出 [block_size]（y[n] = w^T * x[n]）
 * @param  err        误差信号 [block_size]（e[n] = d[n] - y[n]）
 * @param  block_size 本次处理样本数
 *
 * 每次调用后，lms->inst.pCoeffs 中的权值已自动更新。
 * 对于持续自适应场景，连续调用即可——权值会在块间延续。
 */
void DspAdaptive_LMS_Process(DspAdaptive_LMS *lms,
                             const float32_t *src,
                             float32_t       *ref,
                             float32_t       *out,
                             float32_t       *err,
                             uint32_t         block_size);

/*===========================================================================
 * 二、NLMS 自适应滤波器（Normalized LMS — 归一化 LMS）
 *
 *   权值更新公式：w[n+1] = w[n] + (μ / (ε + ||x[n]||²)) * e[n] * x[n]
 *     ||x[n]||² : 输入信号向量的能量（各分量平方和）
 *     ε         : 微小正数，防止除以零（通常在 1e-6 ~ 1e-3）
 *
 *   与 LMS 的关键区别：
 *     - 步长除以输入能量，使收敛速度对输入功率不敏感
 *     - 当输入信号功率波动大时（如扫频信号、语音），NLMS 优于 LMS
 *     - μ 可以取 0<μ<2（理论收敛范围），常用 μ≈0.1~0.5
 *
 *   归一化的数学直觉：
 *     LMS 梯度 = e[n]*x[n]，步长被 x[n] 幅度放大 → 大信号过冲
 *     NLMS 梯度 = e[n]*x[n]/||x||²，方向不变但幅度归一化 → 稳定
 *===========================================================================*/

/** NLMS 滤波器实例（封装 CMSIS-DSP arm_lms_norm_instance_f32） */
typedef struct {
    arm_lms_norm_instance_f32 inst;  /* CMSIS 内部句柄 */
} DspAdaptive_NLMS;

/**
 * @brief  初始化 NLMS 自适应滤波器
 * @param  nlms       滤波器实例指针
 * @param  num_taps   抽头数
 * @param  coeffs     权值缓冲 [num_taps]
 * @param  state      状态缓冲 [num_taps + block_size - 1]
 * @param  mu         步长因子（0<μ<2），推荐 0.1~0.5
 * @param  energy     能量缓冲 [block_size]，用于存储每点输入能量的倒数
 * @param  x0         上次处理的最后一个输入样本（内部状态，初始为 0）
 * @param  block_size 每次处理样本数
 */
void DspAdaptive_NLMS_Init(DspAdaptive_NLMS *nlms,
                           uint16_t          num_taps,
                           float32_t        *coeffs,
                           float32_t        *state,
                           float32_t         mu,
                           float32_t        *energy,
                           float32_t        *x0,
                           uint32_t          block_size);

/**
 * @brief  执行 NLMS 自适应滤波（逐块处理）
 * @param  nlms       已初始化的 NLMS 实例
 * @param  src        输入信号 [block_size]
 * @param  ref        期望信号 [block_size]
 * @param  out        滤波器输出 [block_size]
 * @param  err        误差信号 [block_size]
 * @param  block_size 本次处理样本数
 */
void DspAdaptive_NLMS_Process(DspAdaptive_NLMS *nlms,
                              const float32_t  *src,
                              float32_t        *ref,
                              float32_t        *out,
                              float32_t        *err,
                              uint32_t          block_size);

/*===========================================================================
 * 三、RLS 自适应滤波器（Recursive Least Squares — 递推最小二乘）
 *
 *   RLS 是最小化"指数加权累积误差平方和"的递推算法：
 *     J[n] = Σ λ^(n-i) * |e[i]|² ，i=1..n
 *   λ 是遗忘因子（0<λ≤1）：λ=1 无限记忆（适合时不变系统），
 *   λ<1 逐渐遗忘旧数据（适合跟踪时变系统）
 *
 *   核心递推（每条样本执行一次）：
 *     1) π = P * x          : 计算中间向量（N×N × N×1 = N×1）
 *     2) γ = λ + xᵀ * π     : 标量，规范化因子
 *     3) k = π / γ          : 增益向量（Kalman gain）
 *     4) y = wᵀ * x         : 滤波器输出
 *     5) e = d - y          : 先验误差
 *     6) w = w + k * e      : 权值更新
 *     7) P = (P - k * πᵀ) / λ : 逆相关矩阵更新（Woodbury 矩阵恒等式）
 *
 *   计算复杂度：O(N²)，N=taps。适合阶数 < 100 的场合。
 *
 *   参数选择建议：
 *     - λ = 0.99：快时变系统跟踪
 *     - λ = 0.999：慢时变 / 噪声较大
 *     - λ = 1.0：时不变系统（稳态误差最小）
 *     - δ = 100：P 初始对角值 = 1/δ（大值=初始收敛快但对噪声敏感）
 *     - δ = 1/SNR_linear：理论最优，但通常 10~1000 都可接受
 *
 *   对比 LMS/NLMS：
 *     - 优点：收敛速度与输入自相关特征值分布无关，稳态误差小
 *     - 缺点：O(N²) 复杂度和内存，N 较大时对 M7 也有压力
 *     - 选择：阶数 > 50 → 推荐 NLMS；阶数 < 30 且要求快速收敛 → 推荐 RLS
 *===========================================================================*/

/** RLS 滤波器实例
 *
 *  内存用量（调用方分配）：
 *     w:       taps * 4 bytes
 *     P:       taps*taps * 4 bytes  ← 主要内存开销
 *     x_buf:   taps * 4 bytes
 *     scratch: 2*taps * 4 bytes     ← RLS_Update 内部临时用
 *
 *  示例：taps=32 → 约 4.5 KB；taps=64 → 约 17 KB
 */
typedef struct {
    uint16_t   taps;       /* 滤波器阶数（权值个数）              */
    float      lambda;     /* 遗忘因子 (0.95~0.999)               */
    float      delta;      /* P 初始逆对角 = 1/delta              */
    float     *w;          /* 权值向量 [taps]，调用方分配          */
    float     *P;          /* 逆相关矩阵 [taps*taps]，调用方分配   */
    float     *x_buf;      /* 输入延迟线 [taps]，调用方分配        */
    float     *scratch;    /* 中间缓冲 [2*taps]，调用方分配        */
                            /*   前 taps=π(P*x), 后 taps=k(增益向量) */
    uint8_t    inited;     /* 内部标志：是否已完成初始化           */
} DspAdaptive_RLS;

/**
 * @brief  初始化 RLS 自适应滤波器
 * @param  rls      RLS 实例指针
 * @param  taps     滤波器阶数（权值个数）
 * @param  lambda   遗忘因子，推荐 0.99~0.999
 * @param  delta    P 矩阵初始逆对角值 = 1/delta，
 *                  大值 → 初始收敛快、对噪声敏感；小值 → 初始收敛慢、更稳健
 * @param  w        权值缓冲 [taps]（Init 内部会清零）
 * @param  P        逆相关矩阵缓冲 [taps*taps]（Init 内部初始化为 I/delta）
 * @param  x_buf    输入延迟线缓冲 [taps]（Init 内部会清零）
 * @param  scratch  中间计算缓冲 [2*taps]（用于 π 和 k 临时存储）
 * @return 0=成功, -1=参数非法
 *
 * P 矩阵初始化：P = (1/delta) * I
 *   这意味着在没有任何先验信息时，假设信号的"初始不确定性"为 1/delta
 *   当 delta 取 100 时，P 初始对角 = 0.01，权值初始收敛幅度约 ±0.01
 */
int  DspAdaptive_RLS_Init(DspAdaptive_RLS *rls,
                          uint16_t          taps,
                          float             lambda,
                          float             delta,
                          float            *w,
                          float            *P,
                          float            *x_buf,
                          float            *scratch);

/**
 * @brief  执行 RLS 单步自适应（逐样处理）
 * @param  rls     已初始化的 RLS 实例
 * @param  x       当前输入样本 x[n]
 * @param  d       当前期望样本 d[n]
 * @param  y       输出：滤波器输出 y[n] = w^T * x
 * @param  e       输出：先验误差 e[n] = d[n] - y[n]
 *
 * 算法步骤（每条样本）：
 *   1) 更新输入延迟线：x_buf = [x, x_buf[0..taps-2]]
 *   2) 计算 π = P * x_buf（矩阵乘向量，O(N²)）
 *   3) 计算 γ = λ + x_buf^T * π（内积，O(N)）
 *   4) 计算增益 k = π / γ（O(N)）
 *   5) 计算 y = w^T * x_buf（内积，O(N)）
 *   6) 计算 e = d - y
 *   7) 更新 w = w + k * e（O(N)）
 *   8) 更新 P = (P - k * π^T) / λ（外积 + 矩阵减 + 标量除，O(N²)）
 *
 * 调用方每收到一个新样本就调用一次本函数。
 * 对于批量数据，用 for 循环逐样调用即可——RLS 的递推本就是一阶的。
 */
void DspAdaptive_RLS_Update(DspAdaptive_RLS *rls,
                            float             x,
                            float             d,
                            float            *y,
                            float            *e);

/**
 * @brief  重置 RLS（权值归零，P 重置为 I/delta）
 *
 * 用于系统状态变化后需要重新学习的场景，无需重新 Init。
 */
void DspAdaptive_RLS_Reset(DspAdaptive_RLS *rls);

#endif /* __DSP_ADAPTIVE_H */
