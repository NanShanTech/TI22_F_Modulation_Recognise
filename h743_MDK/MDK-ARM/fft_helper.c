#include "fft_helper.h"
#include "arm_math_types.h"

void rfft_prepare(uint16_t *adc_buffer, float32_t *pDst, uint32_t blockSize){
    for (uint32_t i=0;i<blockSize;i++)
        pDst[i] = (float32_t)adc_buffer[i];
//    float32_t dc_mean;
//    arm_mean_f32(pDst, blockSize, &dc_mean);
//    arm_offset_f32(pDst, -1.0f * dc_mean, pDst, blockSize);
}