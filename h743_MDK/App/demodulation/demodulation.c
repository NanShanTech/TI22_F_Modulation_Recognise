#include "app_types.h"
#include "arm_math.h"
#include "arm_math_types.h"
#include "dsp/basic_math_functions.h"
#include "dsp/fast_math_functions.h"
#include "dsp/statistics_functions.h"
#include "estimaters.h"
#include "lpf_fir.h"
#include <stdint.h>
#include "demodulation.h"

static arm_fir_instance_f32 fir_instance;
void fir_lpf_100k_init(void) {
  arm_fir_init_f32(&fir_instance, FIR_NUM_TAPS, fir_coeffs, fir_state_buffer,
                   FIR_BLOCK_SIZE);
}

void fir_lpf_100k_process_block(const float32_t *pSrc, float32_t *pDst) {
  arm_fir_f32(&fir_instance, pSrc, pDst, FIR_BLOCK_SIZE);
}

void get_iq(float32_t *pSrc, float32_t *pIBuffer, float32_t *pQBuffer,
            float32_t *pBuffer, uint32_t blockSize, float32_t carrier_freq,
            float32_t fs_hz) {
  float32_t Ts = 1.0f / fs_hz;
  for (uint32_t i = 0; i < blockSize; i++)
    pBuffer[i] = pSrc[i] * arm_cos_f32(2 * PI * carrier_freq * (float32_t)i * Ts);
  fir_lpf_100k_process_block(pBuffer, pIBuffer);
  for (uint32_t i = 0; i < blockSize; i++)
    pBuffer[i] = pSrc[i] * arm_sin_f32(2 * PI * carrier_freq * (float32_t)i * Ts);
  fir_lpf_100k_process_block(pBuffer, pQBuffer);
}

void get_envelope(float32_t *pIBuffer, float32_t *pQBuffer, float32_t *pDst, float32_t blockSize) {
  for(uint32_t i=0;i<blockSize;i++){
    float32_t env_square = (pIBuffer[i] * pIBuffer[i]) + (pQBuffer[i] * pQBuffer[i]);
    float32_t envelope;
    arm_sqrt_f32(env_square, &envelope);
    pDst[i] = envelope;
  }
}

void get_delta_f(float32_t *pIBuffer, float32_t *pQBuffer, uint32_t blockSize,
                 float32_t fs_hz, float32_t *pDst) {
  float32_t Ts = 1.0f / fs_hz;
  for (uint32_t i = 1; i < blockSize; i++) {
    float32_t cross =
        (pIBuffer[i - 1] * pQBuffer[i]) - (pIBuffer[i] * pQBuffer[i - 1]);
    float32_t dot =
        (pIBuffer[i - 1] * pIBuffer[i]) + (pQBuffer[i - 1] * pQBuffer[i]) + 1e-9;
    float32_t delta_phi = cross / dot;
    float32_t f = delta_phi / (2 * PI * Ts);
    pDst[i] = f;
  }
}

ModType_t determine_modulation_method(float32_t *pEnvelope, float32_t *pFreq,
                                      float32_t env_cv_gate,
                                      float32_t freq_cv_gate,
                                      uint32_t blockSize) {
  float32_t envelope_mean, freq_mean;
  float32_t envelope_std, freq_std;
  arm_mean_f32(pEnvelope, blockSize, &envelope_mean);
  arm_mean_f32(pFreq, blockSize, &freq_mean);
  arm_std_f32(pEnvelope, blockSize, &envelope_std);
  arm_std_f32(pFreq, blockSize, &freq_std);
  float32_t envelope_cv = envelope_std / (envelope_mean+1e-20);
  float32_t freq_cv = freq_std / (freq_mean+1e-20);
  if (envelope_cv > env_cv_gate) {
    return MOD_AM;
  } else if (freq_cv > freq_cv_gate) {
    return MOD_FM;
  } else {
    return MOD_CW;
  }
}

DemodulationData demodulation(float32_t *pEnvelope, float32_t *pFreq,
                              float32_t *pBuffer1, float32_t *pBuffer2,
                              float32_t fs_hz, ModType_t modulation_type,
                              uint32_t blockSize) {
  float32_t *pDemodulation;
  DemodulationData out_result;
  switch (modulation_type) {
  case MOD_AM:
    pDemodulation = pEnvelope;
    out_result.modetype = MOD_AM;
    break;
  case MOD_FM:
    pDemodulation = pFreq;
    float32_t freq_mean;
    out_result.modetype = MOD_FM;
    break;
  default:
    out_result.modetype = MOD_CW;
    out_result.freq = -114514.0f;
    out_result.amplitude = -114514.0f;
    out_result.m = -114514.0f;
    return out_result;
  }
  float32_t dc_amp;
  arm_mean_f32(pDemodulation, blockSize, &dc_amp);
  dc_amp /= 0.5f; // hann窗相干增益
  for(uint32_t i=0;i<blockSize;i++){
    pDemodulation[i] = pDemodulation[i] - dc_amp;
    pBuffer1[i] = 0.0f;
  }
  arm_hanning_f32(pBuffer1, blockSize);
  arm_mult_f32(pDemodulation, pBuffer1, pBuffer2, blockSize);
  arm_rfft_fast_instance_f32 S;
  arm_rfft_fast_init_4096_f32(&S);
  arm_rfft_fast_f32(&S, pBuffer2, pBuffer1, 0);
  for(uint32_t i=0;i<blockSize;i++)
    pBuffer2[i] = 0.0f;
  arm_cmplx_mag_f32(pBuffer1, pBuffer2, blockSize / 2);
  float32_t max_peak, ma;
  uint32_t max_peak_index;
  for (uint32_t i=0; i<4; i++)
    pBuffer2[i] = 0.0f;
  for (uint32_t i=400;i<blockSize;i++)
    pBuffer2[i] = 0.0f;
  arm_max_f32(pBuffer2, blockSize / 2, &max_peak, &max_peak_index);
  EstimateData demode_data = estimate_freq_amplitude_phase(
      pBuffer1, blockSize, max_peak_index, fs_hz, HANN, AUTO);
  float32_t freq_demod = demode_data.freq_estimated;
  float32_t amp_demod = demode_data.amplitude_estimated;
  int32_t i = freq_demod / 1000;
  float32_t residual_freq = freq_demod - (1000 * i);
  amp_demod *= 2;
  freq_demod -= residual_freq;
  if (residual_freq > 500)
    freq_demod += 1000;
  if (modulation_type == MOD_AM) {
    amp_demod /= dc_amp;
    ma = amp_demod;
  }
  if (modulation_type == MOD_FM)
    ma = amp_demod / freq_demod;
  out_result.freq = freq_demod;
  out_result.amplitude = amp_demod;
  out_result.m = ma;
  return  out_result;
}
