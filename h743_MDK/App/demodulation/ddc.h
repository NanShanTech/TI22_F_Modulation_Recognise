#include "arm_math.h"
#include "arm_math_types.h"
float32_t get_inband_integration(float32_t *pSrc, // 计算带内积分
                                 const float32_t freq_start,
                                 const float32_t freq_end,
                                 const uint32_t blockSize,
                                 const float32_t fs_hz, const uint32_t n_pts);
