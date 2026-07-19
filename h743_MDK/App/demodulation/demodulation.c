#include "app_config.h"
#include "app_types.h"
#include "arm_math.h"
#include "arm_math_types.h"
#include "dsp/filtering_functions.h"
#include "estimaters.h"
#include "lpf_fir.h"
#include <stdint.h>
#include <math.h>
#include "demodulation.h"
#include "app_types.h"
#include "tasks.h"
#include "decimator_128k_coeffs.h"

static arm_fir_instance_f32 fir_instance;
static arm_fir_decimate_instance_f32 decimator_128k;
void fir_lpf_100k_init(void) {
  arm_fir_init_f32(&fir_instance, FIR_NUM_TAPS, fir_coeffs, fir_state_buffer,
                   FIR_BLOCK_SIZE);
}

void decimator_128k_init(void){
  arm_fir_decimate_init_f32(&decimator_128k, DECIMATOR_128K_NUM_TAPS, DECIMATOR_128K_M, decimator_128k_coeffs, decimator_128k_state, DECIMATOR_128K_INPUT_BLOCK_SIZE);
}

void decimator_128k_process(const float32_t *pSrc, float32_t *pDst){
  arm_fir_decimate_f32(&decimator_128k, pSrc, pDst, DECIMATOR_128K_INPUT_BLOCK_SIZE);
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
  envelope_mean += 1e-9;
  freq_mean += 1e-9;
  arm_std_f32(pEnvelope, blockSize, &envelope_std);
  arm_std_f32(pFreq, blockSize, &freq_std);
  float32_t envelope_cv = envelope_std / envelope_mean;
  float32_t freq_cv = freq_std / freq_mean;
  if (envelope_cv > env_cv_gate) {
    return MOD_AM;
  } else if (freq_cv > freq_cv_gate) {
    return MOD_FM;
  } else {
    return MOD_CW;
  }
}

DemodCandidate coherence_demodulation(float32_t *pSrc, float32_t freq_start, float32_t freq_stop, float32_t freq_step, float32_t blockSize, float32_t fs_hz){
  uint32_t freq_start_bin = (uint32_t)(freq_start / freq_step);
  uint32_t freq_stop_bin = (uint32_t)(freq_stop / freq_step + 1);
  float32_t p_max = 0;
  float32_t p_sub_max = 0;
  uint32_t max_freq,sub_max_freq;
  for (float32_t i=freq_start;i<freq_stop;i+=freq_step){
    float32_t ck_sum = 0;
    float32_t sk_sum = 0;
    for (uint32_t j=0;j<blockSize;j++){
      ck_sum += pSrc[j] * arm_cos_f32(2 * PI * i * (float32_t)j / fs_hz);
      sk_sum += pSrc[j] * arm_sin_f32(2 * PI * i * (float32_t)j / fs_hz);
    }
    float32_t p_sum = (ck_sum * ck_sum) + (sk_sum * sk_sum);
    if(p_max < p_sum){
      p_max = p_sum;
      max_freq = i;
    } else if(p_sub_max < p_sum){
      p_sub_max = p_sum;
      sub_max_freq = i;
    }
  }
  float32_t m;// 调制度
  m = sqrtf(p_max) * 2 / blockSize; 
  float32_t x_n_sum = 0;
  for (uint32_t i=0; i<blockSize;i++){
    x_n_sum += pSrc[i] * pSrc[i];
  }
  float32_t rou = 2 * p_max /  (x_n_sum + 1e-9);
  float32_t d = 10 * log10f((p_max + 1e-9) / (p_sub_max + 1e-9));
  DemodCandidate out = {
    .freq = max_freq,
    .power = p_max,
    .rou = rou,
    .D = d,
    .m = m,
  };
  return out;
}

Wave_Struct determine_modulation_method_coherence(DemodCandidate demode_result_am, DemodCandidate demode_result_fm){  
  Wave_Struct out = {
    .mod_type = MOD_CW,
    .carrier_freq = freq_lo,
    .mod_freq = 0,
    .mod_depth = 0,
    .mod_vpp = 0
  };
  uint8_t check_rou_and_d = 0;
  uint8_t is_modulated = 0;
  DemodCandidate modulation_result;
  float32_t am_m = demode_result_am.m;
  am_m *= 2;
  if(am_m >= 0.6){
    am_m -= 0.13;
  } else if(am_m >= 0.5){
    am_m -= 0.07;
  } else if (am_m >= 0.4) {
    am_m -= 0.08;
  } else if(am_m >= 0.3){
    am_m -= 0.055;
  } else{
    am_m -= 0.045;
  }
  float32_t fm_m = demode_result_fm.m / demode_result_fm.freq;
  if (am_m > MA_GATE){
    modulation_result = demode_result_am;
    out.mod_type = MOD_AM;
    out.mod_depth = am_m;
  } else if(fm_m > MF_GATE){
    modulation_result = demode_result_fm;
    out.mod_type = MOD_FM;
    out.mod_depth = fm_m;
   }
  if (out.mod_type != MOD_CW){
    float32_t rou = modulation_result.rou;
    float32_t d = modulation_result.D;
    if (rou < ROU_GATE || d < D_GATE){
      out.mod_type = MOD_CW;
      out.mod_depth = 0;
    }
    else{
      out.mod_freq = modulation_result.freq;
    }
  }
  return out;
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
