/**
 * @file    fft_analyzer.h
 * @brief   FFT 频谱分析 API
 * 
 * 依赖：Cortex-M7 DSP 库 (arm_math.h)
 * 用法：
 *   1. 准备 fftin_t 输入数据（实部填入 ADC 值，虚部清零）
 *   2. 调用 fft_process() 执行 FFT
 *   3. 调用 get_max_3() 获取前三峰值
 *   4. 调用 Rec_wavetype() 识别波形类型
 */

#ifndef __FFT_ANALYZER_H
#define __FFT_ANALYZER_H

#include "app_types.h"
#include "arm_math.h"

/* ---- FFT 核心函数 ---- */

/** 将 ADC 原始数据转换为 FFT 输入（去直流 + 电压定标） */
void fft_prepare(const uint16_t *adc_data, fftin_t *out);

/** 加窗函数（调用前 out 须已填充实部） */
void fft_window(fftin_t *data, const float *window_func);

/** 执行 FFT，结果填入 fftout_t */
void fft_process(fftin_t *data, fftout_t *output);

/** 幅度归一化（除以最大值再乘 norm_val） */
void fft_normalize(fftout_t *result, float norm_val);

/** 获取频率轴（首次调用计算，后续返回静态缓存） */
freqaxis_t *fft_get_axis(void);

/* ---- 峰值搜索与波形分析 ---- */

/** 获取幅度谱最大的三个峰值索引 */
void fft_find_peaks(const fftout_t *mag, peak3_t *peaks);

/** 频率规整到最近整数步长 */
float fft_round_freq(float raw_freq);

/** 识别波形类型（基于谐波比例） */
WaveType_t fft_rec_wavetype(const fftout_t *mag, uint16_t base_idx);

/** 时域峰峰值提取（滑动窗口法） */
float find_vpp(const fftin_t *input);

/** 相位提取 */
void fft_phase_atan(const float *complex_data, uint32_t index, float *phase);

/** 指定谐波最大幅值查找 */
float fft_max_harmonic(const float *mag, uint16_t base_idx, uint8_t harmonic_n);

#endif /* __FFT_ANALYZER_H */
