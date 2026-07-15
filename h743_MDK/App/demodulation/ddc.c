#include "ddc.h"
#include "arm_math_types.h"

float32_t get_inband_integration(float32_t *pSrc, // 计算带内积分
                                 const float32_t freq_start,
                                 const float32_t freq_end,
                                 const uint32_t blockSize,
                                 const float32_t fs_hz, const uint32_t n_pts) {
  uint32_t bin_start = freq_start / (fs_hz / n_pts);
  uint32_t bin_end = freq_end / (fs_hz / n_pts);
  float32_t sum = 0;
  float32_t *pEnd;
  float32_t *pStart = pSrc;
  pEnd = pSrc + bin_end;
  if (bin_end > blockSize)
    pEnd = pSrc + blockSize;
  if (bin_start < 0)
    bin_start = 0;
  while (pStart < pEnd) {
    sum += *pStart;
    pStart++;
  }
  return sum;
}
