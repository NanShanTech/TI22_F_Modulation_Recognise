#ifndef DC_BLOCKER_BIQUAD_H
#define DC_BLOCKER_BIQUAD_H

#include "arm_math.h"

#define DC_BLOCKER_FS_HZ (1024000.0f)
#define DC_BLOCKER_FC_HZ (5000.0f)
#define DC_BLOCKER_FILTER_ORDER (2U)
#define DC_BLOCKER_NUM_STAGES (1U)

/*
 * CMSIS-DSP arm_biquad_cascade_df2T_f32系数格式：
 *
 * {b0, b1, b2, a1, a2}
 *
 * 注意：
 * 这里的a1和a2已经由SciPy符号转换为CMSIS-DSP符号，
 * STM32端不得再次取反。
 */
static const float32_t dc_blocker_coeffs[5U * DC_BLOCKER_NUM_STAGES] = {
    9.785398245e-01f, -1.957079649e+00f, 9.785398245e-01f, 1.956619024e+00f,
    -9.575402737e-01f /* stage 1: b0, b1, b2, a1, a2 */
};

/*
 * DF2T每个二阶节需要两个状态变量：
 * {d11, d12, d21, d22, ...}
 *
 * 该数组不能声明为const。
 */
static float32_t dc_blocker_state[2U * DC_BLOCKER_NUM_STAGES] = {0.0f};
void DCBlocker_Init(void);
extern arm_biquad_cascade_df2T_instance_f32 g_dc_blocker;
#endif
