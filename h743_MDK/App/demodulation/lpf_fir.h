/* FIR low-pass: fs=1.024MHz, cutoff=175kHz, num_taps=26 */
#include "app_config.h"
#include "arm_math_types.h"
#define FIR_NUM_TAPS 26U
#define FIR_COEFF_LEN 28U /* padded to multiple of 4 for Helium */
#define FIR_BLOCK_SIZE FFT_N
#define FIR_STATE_LEN (FIR_NUM_TAPS + 2 * FIR_BLOCK_SIZE - 1)
static float32_t fir_state_buffer[FIR_STATE_LEN];;
static const float32_t fir_coeffs[FIR_COEFF_LEN] =
{
    3.9228037349e-04f,
    -3.2446920522e-04f,
    -3.0645718798e-03f,
    -4.0933634154e-03f,
    2.8703399003e-03f,
    1.5132361092e-02f,
    1.4984970912e-02f,
    -1.2689360417e-02f,
    -4.9897622317e-02f,
    -4.2887557298e-02f,
    5.0700463355e-02f,
    2.0443455875e-01f,
    3.2444196939e-01f,
    3.2444196939e-01f,
    2.0443455875e-01f,
    5.0700463355e-02f,
    -4.2887557298e-02f,
    -4.9897622317e-02f,
    -1.2689360417e-02f,
    1.4984970912e-02f,
    1.5132361092e-02f,
    2.8703399003e-03f,
    -4.0933634154e-03f,
    -3.0645718798e-03f,
    -3.2446920522e-04f,
    3.9228037349e-04f,
    0.0f,
    0.0f
};