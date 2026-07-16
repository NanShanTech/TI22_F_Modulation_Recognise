#ifndef __AD9220_H
#define __AD9220_H

#include "stm32h7xx_hal.h"

#define AD9220_SETTLING_SAMPLES 4U
#define AD9220_CODE_MASK        0x0FFFU

void AD9220_Start_DMA(uint16_t *adc_buffer, uint32_t buffer_length);
void AD9220_Stop_DMA(void);
void AD9220_ConvCpltCallback(void);

#endif
