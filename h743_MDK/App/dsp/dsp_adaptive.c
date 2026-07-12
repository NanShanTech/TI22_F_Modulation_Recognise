/**
 * @file    dsp_adaptive.c
 * @brief   自适应滤波器实现 —— LMS / NLMS / RLS
 *
 * LMS 和 NLMS 直接调用 CMSIS-DSP 的 arm_lms_f32 / arm_lms_norm_f32。
 * RLS 基于 CMSIS-DSP 矩阵运算自实现（CMSIS 无内置 RLS）。
 *
 * 所有自适应算法的共同框架：
 *   输入 x[n] → 滤波器 w → 输出 y[n] → 与期望 d[n] 比较得 e[n] → 用 e[n] 更新 w
 *
 * 各算法仅在"如何用 e[n] 更新 w"这一步不同：
 *   LMS  : w += μ * e * x              （固定步长 × 瞬时梯度）
 *   NLMS : w += μ/(ε+||x||²) * e * x   （步长除以输入能量）
 *   RLS  : w += k * e                  （k = P*x/(λ+xᵀP*x)，Kalman 增益）
 */

#include "dsp_adaptive.h"
#include <string.h>

/*===========================================================================
 * LMS 实现（CMSIS-DSP 薄包装）
 *===========================================================================*/

/**
 * @brief  初始化 LMS 自适应滤波器
 *
 * 调用 arm_lms_init_f32 注册系数/状态/步长到 CMSIS-DSP 实例。
 * 权值初始置零：假设初始对系统一无所知（零模型）。
 * 若已知近似权值（如离线训练结果），可改 coeffs 初始值。
 */
void DspAdaptive_LMS_Init(DspAdaptive_LMS *lms,
                          uint16_t         num_taps,
                          float32_t       *coeffs,
                          float32_t       *state,
                          float32_t        mu,
                          uint32_t         block_size)
{
    if ((lms == NULL) || (coeffs == NULL) || (state == NULL)) {
        return;
    }
    arm_lms_init_f32(&lms->inst, num_taps, coeffs, state, mu, block_size);
}

/**
 * @brief  执行 LMS 自适应滤波
 *
 * arm_lms_f32 内部依次对每个样本执行：
 *   1) 滤波：y = Σ w[k]*x[n-k]（与 FIR 完全相同）
 *   2) 计算误差：e = d - y
 *   3) 更新权值：w[k] += μ * e * x[n-k]
 *
 * 收敛行为取决于 μ 和输入信号功率：
 *   - 输入功率大 → 梯度大 → 加速收敛但可能过冲
 *   - 输入功率小 → 梯度小 → 收敛慢
 *   - 若输入功率波动大，建议换用 NLMS
 */
void DspAdaptive_LMS_Process(DspAdaptive_LMS *lms,
                             const float32_t *src,
                             float32_t       *ref,
                             float32_t       *out,
                             float32_t       *err,
                             uint32_t         block_size)
{
    if ((lms == NULL) || (src == NULL) || (ref == NULL) ||
        (out == NULL) || (err == NULL)) {
        return;
    }
    arm_lms_f32(&lms->inst, (float32_t *)src, ref, out, err, block_size);
}

/*===========================================================================
 * NLMS 实现（CMSIS-DSP 薄包装）
 *===========================================================================*/

/**
 * @brief  初始化 NLMS 自适应滤波器
 *
 * NLMS 相对于 LMS 额外需要：
 *   - energy 缓冲：存储每点输入能量的倒数 1/(ε+||x||²)
 *   - x0：内部状态变量（上次最后一个输入样本，用于跨块衔接）
 *
 * 归一化使得 μ 的取值范围与输入功率无关（理论稳定范围 0<μ<2），
 * 实际推荐 μ=0.1~0.5，更低的值牺牲收敛速度换取更小稳态误差。
 */
void DspAdaptive_NLMS_Init(DspAdaptive_NLMS *nlms,
                           uint16_t          num_taps,
                           float32_t        *coeffs,
                           float32_t        *state,
                           float32_t         mu,
                           float32_t        *energy,
                           float32_t        *x0,
                           uint32_t          block_size)
{
    if ((nlms == NULL) || (coeffs == NULL) || (state == NULL) ||
        (energy == NULL) || (x0 == NULL)) {
        return;
    }
    arm_lms_norm_init_f32(&nlms->inst, num_taps, coeffs,
                          state, mu, block_size);
    /* CMSIS-DSP 的 arm_lms_norm_init_f32 不设置 energy/x0，
       需要调用方在首次 Process 前将 energy 和 *x0 清零 */
    memset(energy, 0, block_size * sizeof(float32_t));
    *x0 = 0.0f;
}

/**
 * @brief  执行 NLMS 自适应滤波
 *
 * arm_lms_norm_f32 内部流程（每条样本）：
 *   1) 滤波：y = Σ w[k]*x[n-k]
 *   2) 计算输入能量：power = ε + x[n]² + x[n-1]² + ... + x[n-N+1]²
 *   3) 计算误差：e = d - y
 *   4) 更新权值：w[k] += (μ/power) * e * x[n-k]
 *
 * 注意：每次调用 Process 后权值已更新，连续调用即可持续自适应。
 * 与 LMS 相比，NLMS 在输入功率变化时保持稳定收敛。
 */
void DspAdaptive_NLMS_Process(DspAdaptive_NLMS *nlms,
                              const float32_t  *src,
                              float32_t        *ref,
                              float32_t        *out,
                              float32_t        *err,
                              uint32_t          block_size)
{
    if ((nlms == NULL) || (src == NULL) || (ref == NULL) ||
        (out == NULL) || (err == NULL)) {
        return;
    }
    arm_lms_norm_f32(&nlms->inst, (float32_t *)src, ref,
                     out, err, block_size);
}

/*===========================================================================
 * RLS 实现（自实现 + CMSIS-DSP 矩阵运算）
 *
 * 参考：安富莱 STM32-V7 DSP 教程 V2.7 自适应滤波章节
 *
 * RLS 相比 LMS/NLMS 的核心创新：
 *   不直接用瞬时梯度，而是维护输入自相关矩阵的逆 P，
 *   利用 Kalman 增益 k(n) 做最优步长调整。
 *   代价：每次更新的计算量从 O(N) 提升到 O(N²)。
 *
 * P 矩阵的递推采用 Woodbury 矩阵恒等式：
 *   P[m+1] = λ⁻¹ * (P[m] - P[m]*x*xᵀ*P[m] / (λ + xᵀ*P[m]*x))
 * 避免了直接矩阵求逆的 O(N³) 开销。
 *
 * 为防止有限精度运算导致 P 失去对称性/正定性，
 * 本实现优先保证数值稳定性（先除再乘、不引入额外的 subtractive cancellation）。
 *===========================================================================*/

/**
 * @brief  初始化 RLS 自适应滤波器
 *
 * 初始化内容：
 *   1) 权值 w 置零（零模型起点）
 *   2) 输入延迟线 x_buf 置零
 *   3) P 矩阵初始化为 (1/delta) * I（对角阵，非对角为零）
 *      - delta 越大 → P 对角越小 → 初始权值更新幅度越小 → 初始收敛越慢
 *      - delta 越小 → P 对角越大 → 初始更"激进"，快速学习但可能震荡
 *   4) 中间缓冲 scratch 置零
 *
 * P 的对角初始化利用了"初始状态信息为零"的假设：
 * 在没有任何先验数据时，Riccati 方程的解就是 I/delta。
 */
int DspAdaptive_RLS_Init(DspAdaptive_RLS *rls,
                         uint16_t          taps,
                         float             lambda,
                         float             delta,
                         float            *w,
                         float            *P,
                         float            *x_buf,
                         float            *scratch)
{
    if ((rls == NULL) || (w == NULL) || (P == NULL) ||
        (x_buf == NULL) || (scratch == NULL) || (taps == 0U)) {
        return -1;
    }

    rls->taps   = taps;
    rls->lambda = lambda;
    rls->delta  = delta;
    rls->w      = w;
    rls->P      = P;
    rls->x_buf  = x_buf;
    rls->scratch= scratch;

    /* 权值、延迟线、scratch 清零 */
    memset(w,   0, (size_t)taps         * sizeof(float));
    memset(x_buf, 0, (size_t)taps      * sizeof(float));
    memset(scratch, 0, (size_t)(2 * taps) * sizeof(float));

    /* P = (1/delta) * I ：初始化为对角阵，非对角元为零 */
    const float p_init = 1.0f / delta;
    const uint32_t n = (uint32_t)taps;
    for (uint32_t i = 0U; i < n * n; i++) {
        P[i] = 0.0f;
    }
    for (uint32_t i = 0U; i < n; i++) {
        P[i * n + i] = p_init;   /* P[i][i] = 1/delta */
    }

    rls->inited = 1U;
    return 0;
}

/**
 * @brief  重置 RLS（保持结构参数不变，清除学习状态）
 *
 * 适用场景：信号环境突变（如切换被测信号源）后，需要"重新学习"。
 * 比重新 Init 更轻量——不改变结构体字段分配，只重置数值。
 */
void DspAdaptive_RLS_Reset(DspAdaptive_RLS *rls)
{
    if ((rls == NULL) || (rls->inited == 0U)) {
        return;
    }
    const uint32_t n = (uint32_t)rls->taps;

    /* 权值和延迟线归零 */
    memset(rls->w,      0, n * sizeof(float));
    memset(rls->x_buf,  0, n * sizeof(float));
    memset(rls->scratch, 0, (size_t)(2U * n) * sizeof(float));

    /* P 重置为对角阵 I/delta */
    const float p_init = 1.0f / rls->delta;
    for (uint32_t i = 0U; i < n * n; i++) {
        rls->P[i] = 0.0f;
    }
    for (uint32_t i = 0U; i < n; i++) {
        rls->P[i * n + i] = p_init;
    }
}

/**
 * @brief  执行 RLS 单步自适应
 *
 * 算法细节（与 dsp_adaptive.h 中描述对应）：
 *
 * 步骤 1: 更新输入延迟线
 *   将当前输入 x 推入延迟线头部，旧数据右移。
 *   x_buf = [x(n), x(n-1), ..., x(n-N+1)]  ← 最新到最旧
 *
 * 步骤 2: π = P * x_buf
 *   使用 CMSIS-DSP arm_mat_mult_f32 做矩阵乘向量。
 *   P 是 N×N 矩阵，x_buf 是 N×1 向量，π 是 N×1 结果。
 *
 * 步骤 3: γ = λ + x_buf^T * π
 *   利用 CMSIS-DSP arm_dot_prod_f32 计算内积。
 *   γ 是标量，用作 Kalman 增益的分母。
 *
 * 步骤 4: k = π / γ
 *   用 CMSIS-DSP arm_scale_f32 做标量除法。
 *   k 的各分量是每个权值的"最优步长"。
 *
 * 步骤 5: y = w^T * x_buf
 *   用 arm_dot_prod_f32 计算滤波输出。
 *
 * 步骤 6: e = d - y
 *   先验误差——在权值更新前计算。
 *
 * 步骤 7: w = w + k * e
 *   k*e 是权值修正量。误差大 → 修正大；误差小 → 修正小。
 *
 * 步骤 8: P = (P - k * x_buf^T * P) / λ  ...实际上 Woodbury 形式
 *   P = (P - k * π^T) / λ
 *   因为 π = P*x_buf，所以 k*π^T = k*(P*x_buf)^T 是 Woodbury 修正项。
 *   先用外积 k*π^T 构造修正矩阵，再从 P 中减去，最后除以 λ。
 *
 * 每条样本的总计算量：约 2.5*N² + 8*N 次浮点运算。
 * 例：N=32 → ~2800 FLOP/样本，M7@480MHz 约 6 μs。
 */
void DspAdaptive_RLS_Update(DspAdaptive_RLS *rls,
                            float             x,
                            float             d,
                            float            *y,
                            float            *e)
{
    if ((rls == NULL) || (rls->inited == 0U) ||
        (y == NULL) || (e == NULL)) {
        return;
    }

    const uint32_t N = (uint32_t)rls->taps;
    const float    lam = rls->lambda;
    float *const   w   = rls->w;
    float *const   P   = rls->P;
    float *const   x_buf = rls->x_buf;
    float *const   pi  = rls->scratch;          /* 前 taps: π 向量 */
    float *const   k   = rls->scratch + N;       /* 后 taps: k 向量 */

    /* ---- 步骤 1: 更新输入延迟线 ---- */
    /* 右移旧数据 [x0,x1,...,x_{N-2}] → [x_{N-1}被丢弃]，头部放新 x */
    for (uint32_t i = N - 1U; i > 0U; i--) {
        x_buf[i] = x_buf[i - 1U];
    }
    x_buf[0U] = x;

    /* ---- 步骤 2: π = P * x_buf（矩阵 × 向量，O(N²)）---- */
    /* 用 CMSIS-DSP arm_mat_mult_f32：需构建矩阵和向量实例 */
    {
        arm_matrix_instance_f32 P_mat, x_vec, pi_vec;
        arm_mat_init_f32(&P_mat,  (uint16_t)N, (uint16_t)N, P);
        arm_mat_init_f32(&x_vec,  (uint16_t)N, 1U,            x_buf);
        arm_mat_init_f32(&pi_vec, (uint16_t)N, 1U,            pi);
        arm_mat_mult_f32(&P_mat, &x_vec, &pi_vec);
    }

    /* ---- 步骤 3: γ = λ + x_bufᵀ * π（内积，O(N)）---- */
    float gamma;
    arm_dot_prod_f32(x_buf, pi, N, &gamma);
    gamma += lam;

    /* 防止 gamma 接近零导致 k 溢出（理论上 lambda>0 和输入非零时不会发生） */
    if (gamma < 1e-8f) {
        gamma = 1e-8f;
    }

    /* ---- 步骤 4: k = π / γ（标量除法，O(N)）---- */
    {
        const float inv_gamma = 1.0f / gamma;
        arm_scale_f32(pi, inv_gamma, k, N);
    }

    /* ---- 步骤 5: y = wᵀ * x_buf（滤波输出，O(N)）---- */
    float y_val;
    arm_dot_prod_f32(w, x_buf, N, &y_val);
    *y = y_val;

    /* ---- 步骤 6: e = d - y（先验误差）---- */
    *e = d - y_val;

    /* ---- 步骤 7: w = w + k * e（权值更新，O(N)）---- */
    /* 直接用循环做 w[i] += k[i] * e，避免额外缓冲分配 */
    for (uint32_t i = 0U; i < N; i++) {
        w[i] += k[i] * (*e);
    }

    /* ---- 步骤 8: P = (P - k * πᵀ) / λ（O(N²)，P 就地更新）---- */
    /* k * πᵀ 是 N×N 外积矩阵： (k*πᵀ)[i][j] = k[i] * π[j] */
    /* 因此 P[i][j] = (P[i][j] - k[i] * π[j]) / λ */
    const float inv_lam = 1.0f / lam;
    for (uint32_t i = 0U; i < N; i++) {
        const float ki = k[i];
        for (uint32_t j = 0U; j < N; j++) {
            /* P[i*N + j] = (P[i][j] - k[i] * pi[j]) / lambda */
            P[i * N + j] = (P[i * N + j] - ki * pi[j]) * inv_lam;
        }
    }
}
