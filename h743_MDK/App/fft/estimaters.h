#include "arm_math.h"
#include "arm_math_types.h"
// 窗函数常量定义
#define RECT 0
#define HANN 1
#define HAMMING 2
#define BLACKMAN 3
#define BLACKMANHARRIS 4
#define FLATTOP 5

// 算法常量定义
#define RIFE 0
#define CANDAN 116
#define GRANKE 1
#define OFFELLI 2
#define ANDRIA_BLACKMAN 3
#define AGREZ 4
#define ANDRIA_FLATTOP 5
#define AUTO 114
#define PARABOLIC 115

extern const float32_t WINDOW_COHERENT_GAIN[];

typedef struct {
  float32_t freq_estimated;
  float32_t amplitude_estimated;
  float32_t phase_estimated;
} EstimateData;

EstimateData estimate_freq_amplitude_phase(const float32_t *pSrc,
                                           const uint32_t blockSize,
                                           const uint32_t peak_bin,
                                           const uint32_t fs_hz,
                                           const uint32_t window_type,
                                           const uint32_t algorithm_type);
