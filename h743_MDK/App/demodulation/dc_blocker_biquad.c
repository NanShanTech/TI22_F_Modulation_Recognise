#include "dc_blocker_biquad.h"

arm_biquad_cascade_df2T_instance_f32 g_dc_blocker;
void DCBlocker_Init(void)
{
    arm_biquad_cascade_df2T_init_f32(
        &g_dc_blocker,
        DC_BLOCKER_NUM_STAGES,
        dc_blocker_coeffs,
        dc_blocker_state
    );
}