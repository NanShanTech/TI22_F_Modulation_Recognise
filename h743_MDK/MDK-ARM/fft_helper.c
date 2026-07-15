#include "fft_helper.h"

void rfft_prepare(uint16_t *adc_buffer, float32_t *pDst, uint32_t blockSize){
    uint16_t *padcBufferStart = adc_buffer;
    uint16_t *padcBufferEnd = adc_buffer + blockSize;
    float32_t *pDstStart = pDst;
    float32_t *pDstEnd = pDst + blockSize;
    while(padcBufferStart < padcBufferEnd && pDstStart < pDstEnd){
        *pDstStart = (float32_t)(*padcBufferStart);
        pDstStart++;
    }
    float32_t dc_mean;
    dc_mean *= -1.0f;
    arm_mean_f32(pDst, blockSize, &dc_mean);
    arm_offset_f32(pDst, dc_mean, pDst, blockSize);
}