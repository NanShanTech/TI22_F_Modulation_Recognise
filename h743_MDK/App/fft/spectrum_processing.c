#include "spectrum_processing.h"
#define INV_LN10 0.43429448190325f

void get_linear_amp_spectrum(const float32_t *pSrc, // 获得线性幅度谱
                             float32_t *pDst, uint32_t numSamples) {
  arm_cmplx_mag_f32(pSrc, pDst, numSamples);
}

void get_db_spectrum(const float32_t *pSrc, float32_t *pDst, uint32_t blockSize,
                     float32_t *pBuffer) {
  float32_t max_val;
  uint32_t max_index;
  arm_max_f32(pSrc, blockSize, &max_val, &max_index);
  max_val = 1.0f / max_val;
  arm_scale_f32(pSrc, max_val, pBuffer, blockSize);
  arm_clip_f32(pBuffer, pDst, 1e-300, 1e5, blockSize);
  arm_vlog_f32(pDst, pBuffer, blockSize); // 这里求得是ln值
  arm_scale_f32(pBuffer, 20.0f * INV_LN10, pDst, blockSize);
}

float32_t estimate_noise_floor(const float32_t *pSrc, uint32_t blockSize,
                               float32_t *pBuffer) {
  if ((blockSize & (blockSize - 1)) != 0) {
    return 114514;
  }
  arm_sort_instance_f32 S;
  memcpy(pBuffer, pSrc, blockSize * sizeof(float32_t));
  arm_sort_init_f32(&S, ARM_SORT_BITONIC, ARM_SORT_ASCENDING);
  arm_sort_f32(&S, pBuffer, pBuffer, blockSize);
  uint32_t middle_index = blockSize / 2; // 求中间索引
  float32_t median_val = pBuffer[middle_index];
  if ((blockSize % 2 == 0) && (middle_index != 0)) {
    median_val += pBuffer[middle_index - 1];
    median_val /= 2;
  }
  float32_t max_val;
  uint32_t max_index;
  arm_max_f32(pSrc, blockSize, &max_val, &max_index);
  if (max_val <= 1e-300) {
    max_val = 1e-300;
  }
  float32_t noise_floor = 20 * log10f(median_val / max_val);
  return noise_floor;
}

void find_peaks_above_noise(const float32_t *pDBspectrum, float32_t margin_dB,
                            uint32_t blockSize, uint32_t *pDst,
                            float32_t noise_floor) {
  uint32_t j = 0;
  for (uint32_t i = 1; i < blockSize - 1; i++) {
    if ((pDBspectrum[i - 1] <= pDBspectrum[i]) &&
        (pDBspectrum[i + 1] <= pDBspectrum[i]) &&
        (pDBspectrum[i] >= noise_floor + margin_dB)) {
      if (j > blockSize / 4) // 最多局部极大值不可能超过 n/4
        break;
      pDst[j] = i;
      j += 1;
    }
  }
}

void peak_dominant_filtering(uint32_t *pIndex, uint32_t dominance_radius,
                             float32_t dominance_ratio, uint32_t blockSize,
                             const float32_t *pDBspectrum, float32_t *pBuffer) {
  memset(pBuffer, 0.0f, blockSize * sizeof(float32_t));
  uint32_t current_peak_index = 0;
  pBuffer[current_peak_index] = 1.0f;
  for (uint32_t i = 1; i < blockSize / 4; i++) {
    uint32_t j = pIndex[i];
    if (j > 0) {
      uint32_t k = pIndex[current_peak_index];
      if (j - k <= dominance_radius) {
        if (pDBspectrum[j] >= pDBspectrum[k]) {
          pBuffer[i] = 1.0f;
          if (pDBspectrum[k] - pDBspectrum[j] <= dominance_ratio)
            pBuffer[current_peak_index] = 0.0f;
          current_peak_index = i;
        }
      } else {
        pBuffer[i] = 1.0f;
        current_peak_index = i;
      }
    }
  }
  for (int i = 0; i < blockSize / 4; i++) {
    if (pBuffer[i] == 0.0f)
      pIndex[i] = 0;
  }
}

void mainlobe_halfwidth_merge(uint32_t *pIndex, uint32_t radius,
                              uint32_t blockSize, const float32_t *pDBspectrum,
                              float32_t *pBuffer) {
  peak_dominant_filtering(pIndex, radius, 0, blockSize, pDBspectrum, pBuffer);
}

void merge_neighboring_peaks(uint32_t *pIndex, uint32_t blockSize,
                             uint32_t mainlobe_width,
                             const float32_t *pDBspectrum, float32_t *pBuffer) {
  uint32_t peak_nums = 0;
  for (uint32_t i = 0; i < blockSize / 4; i++) {
    if (pIndex[i] != 0)
      peak_nums += 1;
  }
  if (peak_nums <= 5) {
    mainlobe_halfwidth_merge(pIndex, mainlobe_width, blockSize, pDBspectrum,
                             pBuffer);
  } else if (peak_nums <= 20) {
    mainlobe_halfwidth_merge(pIndex, mainlobe_width, blockSize, pDBspectrum,
                             pBuffer);
    peak_dominant_filtering(pIndex, 15, 10.0f, blockSize, pDBspectrum, pBuffer);
  } else {
    mainlobe_halfwidth_merge(pIndex, 10, blockSize, pDBspectrum, pBuffer);
    peak_dominant_filtering(pIndex, 26, 15.0f, blockSize, pDBspectrum, pBuffer);
  }
}

void find_peaks(const float32_t *pDBspectrum, float32_t noise_floor,
                uint32_t mainlobe_width, uint32_t blockSize,
                float32_t margin_dB, uint32_t *pDst, float32_t *pBuffer) {
  find_peaks_above_noise(pDBspectrum, margin_dB, blockSize, pDst, noise_floor);
  merge_neighboring_peaks(pDst, blockSize, mainlobe_width, pDBspectrum,
                          pBuffer);
}
