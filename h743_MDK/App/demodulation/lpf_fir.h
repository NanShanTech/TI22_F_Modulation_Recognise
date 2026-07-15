/* FIR low-pass: fs=1.024MHz, cutoff=175kHz, num_taps=26 */
#include "app_config.h"
#include "arm_math_types.h"
#define FIR_NUM_TAPS 26U
#define FIR_COEFF_LEN 28U /* padded to multiple of 4 for Helium */
#define FIR_BLOCK_SIZE FFT_N
#define FIR_STATE_LEN (FIR_NUM_TAPS + 2 * FIR_BLOCK_SIZE - 1)
static float32_t fir_state_buffer[FIR_STATE_LEN];
static const float32_t fir_coeffs[FIR_COEFF_LEN] = {
    0.000392280f, -0.000324469f, -0.003064572f, -0.004093363f, 0.002870340f,
    0.015132361f, 0.014984971f,  -0.012689360f, -0.049897622f, -0.042887557f,
    0.050700463f, 0.204434559f,  0.324441969f,  0.324441969f,  0.204434559f,
    0.050700463f, -0.042887557f, -0.049897622f, -0.012689360f, 0.014984971f,
    0.015132361f, 0.002870340f,  -0.004093363f, -0.003064572f, -0.000324469f,
    0.000392280f, 0.000000000f,  0.000000000f};
