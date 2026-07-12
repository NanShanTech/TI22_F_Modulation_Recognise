/**
 * @file    dsp_analyze.h
 * @brief   增强频谱分析模块 —— RFFT / RMS / SNR / THD / 峰值内插
 *
 * 本模块是对 fft_analyzer 的补充（不做替代），提供：
 *   - 实数 FFT (RFFT)：ADC 数据为纯实数，用 RFFT 比 CFFT 省一半内存和运算
 *   - 峰值频率内插：二次抛物线内插突破 bin 分辨率限制
 *   - 信号质量指标：RMS 有效值 / SNR 信噪比 / THD 总谐波失真
 *
 * 与 fft_analyzer 的分工：
 *   fft_analyzer : CFFT + 加窗 + 前三峰值 + 波形识别 + 时域 Vpp + 相位
 *   dsp_analyze  : RFFT + 峰值内插 + SNR + THD + RMS
 *
 * 两者可独立使用，也可配合——先用 fft_analyzer 做粗测和波形识别，
 * 再用 dsp_analyze 做精细频率估计和信号质量评估。
 *
 * 依赖：
 *   - CMSIS-DSP (arm_math.h)：arm_rfft_fast_f32 / arm_cmplx_mag_f32 / arm_rms_f32 / arm_max_f32
 *   - app_config.h：FFT_N / FREQ_S 频谱参数
 */

#ifndef __DSP_ANALYZE_H
#define __DSP_ANALYZE_H

#include "arm_math.h"
#include "app_config.h"

/*===========================================================================
 * 一、RFFT（实数 FFT）
 *
 *   为什么用 RFFT 而非 CFFT：
 *     ADC 数据是纯实数，虚部恒为零。CFFT 白白计算了一半的乘法。
 *     RFFT 利用实信号的 Hermitian 对称性（X[-k] = X*[k]），
 *     仅计算 0 ~ N/2 的频率分量，运算量和存储量都减半。
 *
 *   以 2048 点实数为例：
 *     输入: 2048 float（实数样本）
 *     输出: 1024 float（幅度谱）—— 只需 N/2 个有效 bin
 *     bin0 置零：DC/Nyquist 打包存放于此，不参与峰值搜索
 *
 *   支持的 RFFT 点数：128, 256, 512, 1024, 2048（CMSIS-DSP 内置）
 *   当前仅初始化 cfg->fft_size 对应点数的实例（节省内存）。
 *===========================================================================*/

/** RFFT 运行实例
 *
 *  典型用法：
 *   1. 定义一个 DspAnalyze 实例（全局或 static）
 *   2. DspAnalyze_Init(&ctx, FFT_N) 初始化 RFFT 实例
 *   3. DspAnalyze_RFFT_Mag(&ctx, adc_data, mag_out) 获取幅度谱
 */
typedef struct {
    arm_rfft_fast_instance_f32 rfft_inst;  /* CMSIS RFFT 实例句柄 */
    uint32_t                   fft_size;   /* 当前配置的 FFT 点数     */
    uint8_t                    inited;     /* 初始化标志              */
} DspAnalyze;

/**
 * @brief  初始化频谱分析上下文
 * @param  ctx      分析上下文指针
 * @param  fft_size FFT 点数（必须是 128/256/512/1024/2048 之一）
 * @return 0=成功, -1=点数不支持
 *
 * 内部调用 arm_rfft_fast_init_f32 初始化 CMSIS-DSP RFFT 实例。
 * 每个点数对应一个预计算的旋转因子表（由 CMSIS-DSP 管理）。
 */
int  DspAnalyze_Init(DspAnalyze *ctx, uint32_t fft_size);

/**
 * @brief  实数 FFT 取幅度谱
 * @param  ctx     已初始化的分析上下文
 * @param  in      输入实数样本 [fft_size]
 * @param  scratch 临时缓冲 [fft_size]，由调用方提供（RFFT 内部需要复数工作区）
 * @param  mag     输出幅度谱 [fft_size/2]，bin0 已置零
 * @return 0=成功, -1=参数非法或未初始化
 *
 * 内部执行：
 *   1) arm_rfft_fast_f32: 实数 → 复数频谱（存入 scratch）
 *   2) arm_cmplx_mag_f32: 复数频谱 → 幅度谱
 *   3) mag[0] = 0: 清除 DC/Nyquist bin
 *
 * 计算复杂度：约 (N/2)*log2(N) 次乘法。
 * 2048 点 RFFT ≈ 11k MAC ops，M7@480MHz 约 23 μs（含 CMSIS 开销约 50~80 μs）。
 */
int  DspAnalyze_RFFT_Mag(DspAnalyze     *ctx,
                         const float32_t *in,
                         float32_t       *scratch,
                         float32_t       *mag);

/**
 * @brief  查找幅度谱峰值
 * @param  mag       幅度谱 [len]
 * @param  len       幅度谱长度（通常 = fft_size/2）
 * @param  peak_bin  输出：峰值所在 bin 索引
 * @param  peak_val  输出：峰值幅度值
 *
 * 薄包装 arm_max_f32。bin0 已被清除，不影响峰值搜索。
 */
void DspAnalyze_FindPeak(const float32_t *mag, uint32_t len,
                         uint32_t *peak_bin, float32_t *peak_val);

/*===========================================================================
 * 二、峰值频率内插
 *
 *   问题：FFT 的 bin 分辨率 = Fs/N，在 1.024MHz/2048=500Hz
 *   如果信号频率恰好落在两个 bin 之间（如 10250 Hz），
 *   最大 bin 只能指示 10000 Hz 或 10500 Hz，误差可达 ±250 Hz。
 *
 *   解决：利用峰值 bin 及其两侧邻居的幅度，通过二次抛物线拟合
 *   估计真正峰值位置（即"频率插值"），可将精度提升到 bin 分辨率的
 *   十分之一甚至更好（前提：信噪比足够高，加窗正确）。
 *
 *   抛物线内插公式（三点拟合顶点）：
 *     delta = 0.5 * (L - R) / (L - 2*P + R)
 *     其中: P=峰值幅度, L=左侧邻居幅度, R=右侧邻居幅度
 *     插值后频率 = (peak_bin + delta) * Fs / N
 *===========================================================================*/

/**
 * @brief  峰值频率抛物线内插
 * @param  mag      幅度谱（至少包含 peak_bin-1, peak_bin, peak_bin+1）
 * @param  len      幅度谱长度
 * @param  peak_bin FFT 峰值 bin 索引（来自 FindPeak）
 * @param  fs_hz    采样率 (Hz)
 * @param  fft_size FFT 点数
 * @return 内插后的精确频率 (Hz)
 *
 * 精度说明：
 *   - 理想单频信号 + 矩形窗 → 误差 < 0.01 bin
 *   - 实际信号 + Hann 窗 → 误差 < 0.1 bin（窗函数主瓣略宽）
 *   - 存在邻近干扰频率时 → 内插可能失效（bin 混叠），需先滤波
 *
 * 边界处理：peak_bin 在 0 或 len-1 时无法内插，直接返回离散 bin 频率。
 */
float DspAnalyze_InterpFreq(const float32_t *mag, uint32_t len,
                            uint32_t peak_bin,
                            float    fs_hz, uint32_t fft_size);

/*===========================================================================
 * 三、RMS 有效值
 *
 *   RMS (Root Mean Square) 是信号能量的均方根度量：
 *     RMS = sqrt( (1/N) * Σ x[n]² )
 *
 *   对于已知波形，RMS 与峰值的关系：
 *     正弦波: Vrms = Vpeak / √2 ≈ 0.707 * Vpeak
 *     方波:   Vrms = Vpeak（理想方波）
 *     三角波: Vrms = Vpeak / √3 ≈ 0.577 * Vpeak
 *===========================================================================*/

/**
 * @brief  计算信号 RMS 有效值
 * @param  x   信号样本
 * @param  n   样本数
 * @return RMS 值（与输入同量纲）
 * 薄包装 arm_rms_f32
 */
float DspAnalyze_RMS(const float32_t *x, uint32_t n);

/*===========================================================================
 * 四、SNR 信噪比估算
 *
 *   SNR = 10 * log10(Ps / Pn)  [dB]
 *     Ps = 信号功率 = (峰值幅度 * 窗增益补偿)²
 *     Pn = 噪声功率 = 其余 bin 幅度的均方值（排除 DC、信号 bin 及其邻域）
 *
 *   方法：频域法（基于幅度谱分段统计）
 *     信号 bin：峰值所在位置
 *     噪声 bin：所有非信号邻近 bin 的平均功率
 *
 *   注意：这是简化估算，不是严格意义的 SNR（严格 SNR 需要已知原始无噪信号）。
 *   对于 TI 电赛场景——已知信号频率范围时，本估算足够指导判断。
 *===========================================================================*/

/**
 * @brief  频域 SNR 估算
 * @param  mag      幅度谱 [fft_size/2]
 * @param  len      幅度谱长度
 * @param  peak_bin 信号峰值 bin 索引
 * @param  guard    保护带宽（bin 数），信号 bin 左右各 guard 个 bin 不计入噪声
 * @return SNR 估算值 (dB)
 *
 * 算法：
 *   1) 信号功率 = mag[peak_bin]²
 *   2) 噪声区域 = [guard+1, peak_bin-guard-1] ∪ [peak_bin+guard+1, len-1]
 *   3) 噪声功率 = 噪声区域幅度的均方值
 *   4) SNR = 10 * log10(信号功率 / 噪声功率)
 */
float DspAnalyze_SNR(const float32_t *mag, uint32_t len,
                     uint32_t peak_bin, uint32_t guard);

/*===========================================================================
 * 五、THD 总谐波失真
 *
 *   THD = sqrt(Σ(A_h²)) / A_fund  （h = 2,3,...,H）
 *     或常用对数表示: THD_dB = 20 * log10(THD)
 *
 *   物理意义：信号被非线性系统处理后，会产生基频的整数倍谐波。
 *   THD 衡量"谐波总能量相对于基波的占比"。
 *
 *   典型值：
 *     高质量音频 DAC: THD < 0.001% (-100 dB)
 *     普通运放缓冲:    THD < 0.1%  (-60 dB)
 *     过驱/削波:        THD > 10%  (-20 dB 以上)
 *
 *   TI 电赛场景：THD 用于评估信号源质量或检测非线性失真故障。
 *===========================================================================*/

/**
 * @brief  频域 THD 计算
 * @param  mag       幅度谱 [fft_size/2]
 * @param  len       幅度谱长度
 * @param  peak_bin  基频 bin 索引
 * @param  max_harm  最高谐波次数（通常 5~10，电赛推荐 5）
 * @param  fft_size  FFT 点数（用于计算谐波 bin 位置）
 * @return THD 比率 (0~1)，乘以 100 得百分比
 *
 * 算法：
 *   对 h=2..max_harm:
 *     harm_bin ≈ h * peak_bin（取整，并在 ±1 bin 内搜索最大值）
 *     harm_pow += mag[harm_bin]²（各谐波功率求和）
 *   基波功率 A_fund = mag[peak_bin]
 *   THD = sqrt(harm_pow) / A_fund
 *
 * 注意事项：
 *   - 如果信号未经抗混叠滤波，高次谐波可能折叠到低频频段（欠采样混叠），
 *     导致 THD 计算偏高
 *   - 电赛评测标准通常取 H=5（到第 5 次谐波），因为更高次能量通常可忽略
 */
float DspAnalyze_THD(const float32_t *mag, uint32_t len,
                     uint32_t peak_bin, uint32_t max_harm,
                     uint32_t fft_size);

#endif /* __DSP_ANALYZE_H */
