#include "arm_math.h"
#include "arm_math_types.h"
// 窗函数常量
#define RECT 0x00
#define HANN 0x01
#define HAMMING 0x02
#define BLACKMAN 0x03
#define BLACKMANHARRIS 0x04
#define FLATTOP 0x05

void get_linear_amp_spectrum(const float32_t *pSrc, float32_t *pDst,
                             uint32_t numSamples); // 获得线性幅度谱

void get_db_spectrum(const float32_t *pSrc, float32_t *pDst, uint32_t blockSize,
                     float32_t *pBuffer); // 获取db谱

float32_t estimate_noise_floor(const float32_t *pSrc, uint32_t blockSize,
                               float32_t *pBuffer);

void find_peaks(const float32_t *pDBspectrum, float32_t noise_floor,
                uint32_t mainlobe_width, uint32_t blockSize,
                float32_t margin_dB, uint32_t *pDst,
                float32_t *pBuffer); // 在原始频谱中找到真实的频谱峰值
