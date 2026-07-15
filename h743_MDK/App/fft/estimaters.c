#include "estimaters.h"
#include <math.h>

const float32_t WINDOW_COHERENT_GAIN[] = {1, 0.5, 0.54, 0.42, 0.35875, 0.2155};

float32_t estimate_phase(const float32_t angle, const float32_t freq_delta,
                         const uint32_t n_pts) {
  return angle - PI * freq_delta * (n_pts - 1) / n_pts;
}

float32_t cmplx_mag_simple(const float32_t real, const float32_t img) {
  float32_t real_square = real * real;
  float32_t img_square = img * img;
  float32_t summed_square = real_square + img_square;
  float32_t mag = sqrtf(summed_square);
  return mag;
}

EstimateData parabolic_interp(const float32_t *pSrc, uint32_t peak_bin,
                              const float32_t fs_hz, const float32_t blockSize,
                              const float32_t coherent_gain) {
  uint32_t n_pts = blockSize / 2;
  float32_t y_minus1 =
      cmplx_mag_simple(pSrc[2 * (peak_bin - 1)], pSrc[2 * (peak_bin - 1) + 1]);
  float32_t y_0 = cmplx_mag_simple(pSrc[2 * peak_bin], pSrc[2 * peak_bin + 1]);
  float32_t y_plus1 =
      cmplx_mag_simple(pSrc[2 * (peak_bin + 1)], pSrc[2 * (peak_bin + 1) + 1]);
  float32_t freq_delta =
      0.5 * (y_minus1 - y_plus1) / (y_minus1 - 2 * y_0 + y_plus1);
  float32_t a = (y_minus1 + y_plus1 - 2 * y_0);
  float32_t y_peak = y_0 - a * (freq_delta * freq_delta);
  if (peak_bin > n_pts / 2)
    peak_bin = (n_pts / 2) - peak_bin;
  float32_t freq_estimated = (peak_bin + freq_delta) * (fs_hz / n_pts);
  float32_t amplitude_estimated = y_peak / (n_pts * coherent_gain);
  float32_t angle_ori;
  arm_atan2_f32(pSrc[2 * peak_bin + 1], pSrc[2 * peak_bin], &angle_ori);
  float32_t phase_estimated = estimate_phase(angle_ori, freq_delta, n_pts);
  EstimateData estimate_data;
  estimate_data.freq_estimated = freq_estimated;
  estimate_data.amplitude_estimated = amplitude_estimated;
  estimate_data.phase_estimated = phase_estimated;
  return estimate_data;
}

float32_t delta_rect_candan(const float32_t *pSrc, uint32_t peak_bin) {
  float32_t y_minus1[2], y_0[2], y_plus1[2];
  float32_t result_cmplx[2];
  y_minus1[0] = pSrc[2 * (peak_bin - 1)];
  y_minus1[1] = pSrc[2 * (peak_bin - 1) + 1];
  y_0[0] = pSrc[2 * peak_bin];
  y_0[1] = pSrc[2 * peak_bin + 1];
  y_plus1[0] = pSrc[2 * (peak_bin + 1)];
  y_plus1[1] = pSrc[2 * (peak_bin + 1) + 1];
  float32_t numerator[2], denominator[2];
  numerator[0] = y_minus1[0] - y_plus1[0];
  numerator[1] = y_minus1[1] - y_plus1[0];
  denominator[0] = 2 * y_0[0] - y_minus1[0] - y_plus1[0];
  denominator[1] = 2 * y_0[1] - y_minus1[1] - y_plus1[1];
  denominator[1] *= -1; // 求共轭
  float32_t scale =
      denominator[0] * denominator[0] - denominator[1] * denominator[1];
  arm_cmplx_mult_cmplx_f32(numerator, denominator, result_cmplx, 1);
  result_cmplx[0] /= scale;
  return result_cmplx[0];
}

typedef struct {
  float32_t r;
  float32_t alpha;
} r_and_alpha;

r_and_alpha get_r_and_alpha(const float32_t *pSrc, uint32_t peak_bin) {
  float32_t y_minus1 =
      cmplx_mag_simple(pSrc[2 * (peak_bin - 1)], pSrc[2 * (peak_bin - 1) + 1]);
  float32_t y_0 = cmplx_mag_simple(pSrc[2 * peak_bin], pSrc[2 * peak_bin + 1]);
  float32_t y_plus1 =
      cmplx_mag_simple(pSrc[2 * (peak_bin + 1)], pSrc[2 * (peak_bin + 1) + 1]);
  float32_t r;
  if (y_plus1 >= y_minus1) {
    r = 1.0f;
  } else {
    r = -1.0f;
  }
  float32_t alpha;
  if (r == 1.0f) {
    alpha = y_plus1 / y_0;
  } else if (r == -1.0f) {
    alpha = y_minus1 / y_0;
  } else {
    alpha = -114514.0f;
  }
  r_and_alpha out;
  out.r = r;
  out.alpha = alpha;
  return out;
}

float32_t delta_rect_rife(const float32_t r, const float32_t alpha) {
  return r * alpha / (1 + alpha);
}

float32_t delta_hann_granke(const float32_t r, const float32_t alpha) {
  return r * (2 * alpha - 1) / (alpha + 1);
}

float32_t delta_hamming_offelli(const float32_t r, const float32_t alpha) {
  return r * (alpha - 0.54) / (0.46 * alpha + 0.54);
}

float32_t delta_blackman_andria(const float32_t r, const float32_t alpha) {
  return r * (3 * alpha - 1.5) / (alpha + 1.5);
}

float32_t delta_blackmanharris_agrez(const float32_t r, const float32_t alpha) {
  return r * (4 * alpha - 2) / (alpha + 2);
}

float32_t delta_flattop_andria(const float32_t r, const float32_t alpha) {
  return r * (5.5 * alpha - 4.5) / (alpha + 1);
}

float32_t _sinc_delta(const float32_t delta) {
  return PI * delta / arm_sin_f32(PI * delta);
}

float32_t _m_plus_n_delta_square(const float32_t m, const float32_t n,
                                 const float32_t delta) {
  return m + n * delta * delta;
}

float32_t amp_rect(const float32_t x_k0, const float32_t freq_delta) {
  return x_k0 * _sinc_delta(freq_delta);
}

float32_t amp_hann(const float32_t x_k0, const float32_t freq_delta) {
  return x_k0 * _sinc_delta(freq_delta) *
         _m_plus_n_delta_square(1, -1, freq_delta);
}

float32_t amp_hamming(const float32_t x_k0, const float32_t freq_delta) {
  return x_k0 * _sinc_delta(freq_delta) *
         (_m_plus_n_delta_square(1, -1, freq_delta) /
          _m_plus_n_delta_square(1, -0.148, freq_delta));
}

float32_t amp_blackman(const float32_t x_k0, const float32_t freq_delta) {
  return x_k0 * _sinc_delta(freq_delta) *
         (_m_plus_n_delta_square(1, -1, freq_delta) *
          _m_plus_n_delta_square(4, -1, freq_delta) /
          _m_plus_n_delta_square(4, -1.69, freq_delta));
}

float32_t amp_blackmanharris(const float32_t x_k0, const float32_t freq_delta) {
  return x_k0 * _sinc_delta(freq_delta) *
         (_m_plus_n_delta_square(1, -1, freq_delta) *
          _m_plus_n_delta_square(1, -0.25, freq_delta) *
          _m_plus_n_delta_square(1, -0.111, freq_delta));
}

float32_t amp_flattop(const float32_t x_k0, const float32_t freq_delta) {
  float32_t freq_delta_2 = freq_delta * freq_delta;
  float32_t freq_delta_4 = freq_delta_2 * freq_delta_2;
  return x_k0 * (1 + 0.02 * freq_delta_2 + 0.0005 * freq_delta_4);
}

typedef float32_t (*delta_func_t)(float32_t, float32_t);
const delta_func_t _DELTA_METHODS[] = {
    delta_rect_rife,       delta_hann_granke,          delta_hamming_offelli,
    delta_blackman_andria, delta_blackmanharris_agrez, delta_flattop_andria};

const delta_func_t _AMP_METHODS[] = {amp_rect,           amp_hann,
                                     amp_hamming,        amp_blackman,
                                     amp_blackmanharris, amp_flattop};

EstimateData estimate_freq_amplitude_phase(const float32_t *pSrc,
                                           const uint32_t blockSize,
                                           const uint32_t peak_bin,
                                           const uint32_t fs_hz,
                                           const uint32_t window_type,
                                           const uint32_t algorithm_type) {
  uint32_t n_pts = blockSize / 2;
  EstimateData out;
  if ((peak_bin == 0) || (peak_bin > n_pts - 2)) {
    out.amplitude_estimated = -114514.0f;
    out.phase_estimated = -114514.0f;
    return out;
  }
  float32_t coherent_gain = WINDOW_COHERENT_GAIN[window_type];
  float32_t x_k0 = cmplx_mag_simple(pSrc[2 * peak_bin], pSrc[2 * peak_bin + 1]);
  float32_t angle_ori, freq_delta, amp_estimated, phase_estimated;
  r_and_alpha ralpha;
  arm_atan2_f32(pSrc[2 * peak_bin + 1], pSrc[2 * peak_bin], &angle_ori);
  switch (algorithm_type) {
  case PARABOLIC:
    return parabolic_interp(pSrc, peak_bin, fs_hz, blockSize, coherent_gain);
  case CANDAN:
    if (window_type != RECT) {
      out.amplitude_estimated = -114514.0f;
      out.phase_estimated = -114514.0f;
      return out;
    }
    freq_delta = delta_rect_candan(pSrc, peak_bin);
    break;
  case AUTO:
    switch (window_type) {
    case RECT:
      freq_delta = delta_rect_candan(pSrc, peak_bin);
      break;
    case HANN:
      ralpha = get_r_and_alpha(pSrc, peak_bin);
      float32_t r = ralpha.r;
      float32_t alpha = ralpha.alpha;
      freq_delta = delta_hann_granke(r, alpha);
      break;
    default:
      return parabolic_interp(pSrc, peak_bin, fs_hz, blockSize, coherent_gain);
    }
  default:
    if (algorithm_type == AUTO)
      break; // 特殊情况排除
    ralpha = get_r_and_alpha(pSrc, peak_bin);
    float32_t r = ralpha.r;
    float32_t alpha = ralpha.alpha;
    freq_delta = _DELTA_METHODS[algorithm_type](r, alpha);
    break;
  }
  amp_estimated = _AMP_METHODS[window_type](x_k0, freq_delta);
  amp_estimated /= n_pts * coherent_gain;
  phase_estimated = estimate_phase(angle_ori, freq_delta, n_pts);
  out.freq_estimated = (peak_bin + freq_delta) * (fs_hz / (float32_t)n_pts);
  out.amplitude_estimated = amp_estimated;
  out.phase_estimated = phase_estimated;
  return out;
}
