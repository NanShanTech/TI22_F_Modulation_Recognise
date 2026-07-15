#include "arm_math_types.h"
#include "app_types.h"

typedef struct {
  float32_t freq;
  float32_t amplitude;
  float32_t m;
  ModType_t modetype;
} DemodulationData;

void fir_lpf_100k_init(void);

void fir_lpf_100k_process_block(const float32_t *pSrc, float32_t *pDst);

void get_iq(float32_t *pSrc, float32_t *pIBuffer, float32_t *pQBuffer,
            float32_t *pBuffer, uint32_t blockSize, float32_t carrier_freq,
            float32_t fs_hz);

void get_envelope(float32_t *pIBuffer, float32_t *pQBuffer, float32_t *pBuffer1,
                  float32_t *pBuffer2, float32_t *pDst, float32_t blockSize);           

void get_delta_f(float32_t *pIBuffer, float32_t *pQBuffer, uint32_t blockSize,
                 float32_t fs_hz, float32_t *pDst);

ModType_t determine_modulation_method(float32_t *pEnvelope, float32_t *pFreq,
                                      float32_t env_cv_gate,
                                      float32_t freq_cv_gate,
                                      uint32_t blockSize);

DemodulationData demodulation(float32_t *pEnvelope, float32_t *pFreq,
                              float32_t *pBuffer1, float32_t *pBuffer2,
                              float32_t fs_hz, ModType_t modulation_type,
                              uint32_t blockSize);