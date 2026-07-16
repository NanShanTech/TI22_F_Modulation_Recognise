/**
 * @file    adc_task.h
 * @brief   ADC + DMA 采集与 FFT 分析（单缓冲模式）
 *
 *   ADC DMA → g_adc_buffer[0..N-1] 填满 → HAL_ADC_ConvCpltCallback →
 *   停 ADC → FFT → 重开 ADC → 等下一帧
 *
 *   用法：
 *     ADC_Task_Init(&htim3, &hadc1);
 *     ADC_Task_Start();
 */

#ifndef __ADC_TASK_H
#define __ADC_TASK_H

#include "stm32h7xx_hal.h"
#include "app_types.h"
#include "arm_math_types.h"

/* ---- ADC DMA 缓冲区（AXI SRAM，non-cacheable）---- */
extern __attribute__((section(".AXI_SRAM"))) uint16_t g_adc_buffer[FFT_N];

/* ---- DMA 一帧完成标志（ISR 中置 1，主循环清 0）---- */
extern volatile uint8_t g_adc_dma_done;

/* ---- API ---- */
void ADC_Task_Init (TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc);
void ADC_Task_Start(void);
void ADC_Task_Stop (void);
void ADC_Task_SetSpeed(Wave_Struct *wave);
float32_t ADC_Task_RFFT(uint16_t *adc_buffer, float32_t *buffer,
                        float32_t *pDst, uint32_t blockSize);
#endif
