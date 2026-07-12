/*
 * 设计原则：
 *   - 仅包含 <stdint.h>，不引入 HAL/DSP 等重型头文件
 *   - 所有结构体按功能分组，便于查找
 *   - 使用 float 而非 float32_t，避免绑定 ARM DSP 类型
*/

#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include <stdint.h>

/*===========================================================================
 * 一、波形与信号分析类型
 *===========================================================================*/

/** 波形类型枚举 */
typedef enum {
    WAVE_SINE = 0,      // 正弦波
    WAVE_SQUARE,        // 方波
    WAVE_TRIANGLE,      // 三角波
    WAVE_UNKNOWN        // 未知 / 初始状态
} WaveType_t;

/** 调制类型枚举 */
typedef enum {
    MOD_FM = 0,     // 调频
    MOD_AM,         // 调幅
    MOD_CW          // 等幅报
} ModType_t;

/** 波形测量结果（载波 + 调制参数 + 波形类型） */
typedef struct {
    float     carrier_freq;   // 载波频率 (Hz)
    float     mod_freq;       // 调制频率 (Hz)
    ModType_t mod_type;       // 调制类型
    float     mod_depth;      // 调制度
    float     mod_vpp;        // 调制波形峰峰值 (V)

    WaveType_t Wave_type;
} Wave_Struct;

/*===========================================================================
 * 二、FFT 专用类型
 *===========================================================================*/
#include "app_config.h"  // 仅用于获取 FFT_N

#define FFT_2N  (FFT_N * 2)

/** FFT 输入（复数交错: real[0], imag[0], real[1], imag[1]...） */
typedef struct {
    float cmp[FFT_2N];
} fftin_t;

/** FFT 输出（幅度谱 + 相位谱） */
typedef struct {
    float phase[FFT_2N];
    float mag[FFT_N_2];
} fftout_t;

/** 前三大峰值索引 */
typedef struct {
    uint16_t index[3];
} peak3_t;

/** 频率轴 */
typedef struct {
    float axis[FFT_N_2];
} freqaxis_t;

/*===========================================================================
 * 三、频率测量状态类型
 *===========================================================================*/

/** 测频模式 */
typedef enum {
    FMODE_PERIOD = 0,   // 测周法（低频高精度）
    FMODE_COUNT  = 1    // 测频法（高频）
} FreqMode_t;

/*===========================================================================
 * 四、AD9910 波形枚举
 *===========================================================================*/

typedef enum {
    AD9910_WAVE_TRI  = 0,
    AD9910_WAVE_SQR  = 1,
    AD9910_WAVE_SINC = 2,
} AD9910_Wave_t;

typedef enum {
    DAC_AWG_MODE_STOPPED = 0,
    DAC_AWG_MODE_DC,
    DAC_AWG_MODE_WAVE
} DacAwgMode_t;

typedef enum {
    DAC_AWG_WAVE_SINE = 0,
    DAC_AWG_WAVE_TRIANGLE,
    DAC_AWG_WAVE_SQUARE,
    DAC_AWG_WAVE_SAWTOOTH
} DacAwgWave_t;

typedef enum {
    DAC_AWG_OK = 0,
    DAC_AWG_ERR_PARAM,
    DAC_AWG_ERR_RANGE,
    DAC_AWG_ERR_FREQ,
    DAC_AWG_ERR_HAL
} DacAwgStatus_t;

typedef struct {
    DacAwgMode_t mode;
    DacAwgWave_t wave;
    float requested_freq_hz;
    float actual_freq_hz;
    float freq_error_hz;
    float sample_rate_hz;
    uint32_t vpp_mv;
    uint32_t offset_mv;
    uint32_t dc_mv;
    uint16_t duty_permille;
    uint16_t table_len;
    uint16_t tim_prescaler;
    uint16_t tim_period;
    uint8_t running;
} DacAwgState_t;

/*===========================================================================
 * 五、DSP 信号处理类型
 *===========================================================================*/

/** 窗函数类型枚举（与 WindowsGeneration.m MATLAB 脚本联动） */
typedef enum {
    DSP_WIN_NONE = 0,        /* 矩形窗（不加窗），频率分辨率最高、泄漏最大    */
    DSP_WIN_HANN,            /* 汉宁窗，最常用，泄漏低、主瓣适中              */
    DSP_WIN_HAMMING,         /* 海明窗，旁瓣抑制优于 Hann，但远旁瓣不衰减     */
    DSP_WIN_BLACKMAN,        /* 布莱克曼窗，旁瓣极低，主瓣宽                  */
    DSP_WIN_BLACKMAN_HARRIS, /* 布莱克曼-哈里斯窗，高动态范围分析             */
    DSP_WIN_NUTTALL,         /* 纳托尔窗，旁瓣衰减极快                        */
    DSP_WIN_FLATTOP,         /* 平顶窗，幅值测量最准（但频率分辨率最低）      */
    DSP_WIN_COUNT
} DspWinType_t;

/** RLS 自适应滤波器实例
 *
 *  所有缓冲区由调用方提供（遵循 CMSIS-DSP 惯例），
 *  总内存开销 ≈ (taps² + 4*taps) * 4 bytes
 *  例：taps=32 → ~4.5 KB，taps=50 → ~10.6 KB
 */
typedef struct {
    uint16_t   taps;       /* 滤波器阶数（权值个数）              */
    float      lambda;     /* 遗忘因子 (0.95~0.999)               */
    float      delta;      /* P 初始逆对角 = 1/delta              */
    float     *w;          /* 权值 [taps]，调用方分配              */
    float     *P;          /* 逆相关矩阵 [taps*taps]，调用方分配   */
    float     *x_buf;      /* 输入延迟线 [taps]，调用方分配        */
    float     *scratch;    /* 中间缓冲 [2*taps]（π + k 临时）     */
    uint8_t    inited;     /* 初始化标志                          */
} DspRLS_t;

#endif /* __APP_TYPES_H */
