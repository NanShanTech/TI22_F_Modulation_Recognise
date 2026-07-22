/**
 * @file    adc_task.c
 * @brief   ADC + DMA 采集与 FFT 任务实现（单缓冲模式）
 *
 *   ADC DMA → g_adc_buffer[0..N-1] → ConvCpltCallback 置 g_adc_dma_done →
 *   主循环检测 g_adc_dma_done → 停 ADC → FFT → 重开 ADC
 */

#include "adc_task.h"
#include "ad9220.h"
#include "app_config.h"
#include "arm_math_types.h"
#include "ddc.h"
#include "dma.h"
#include "dsp/complex_math_functions.h"
#include "dsp/transform_functions.h"
#include "stm32h7xx_hal.h"
#include "tasks.h"
#include "tim.h"
#include <stdint.h>
#include <string.h>

/* ---- 硬件句柄 ---- */
/* ---- DMA 缓冲区（AXI SRAM，MPU 已设为 non-cacheable）---- */
__attribute__((section(".AXI_SRAM"), aligned(32))) uint16_t
    g_adc_buffer[FFT_N + AD9220_SETTLING_SAMPLES];

/* ---- DMA 完成标志（ISR 写，主循环读）---- */
volatile uint8_t g_adc_dma_done;

/* ---- FFT 工作缓存 ---- */
static fftin_t g_fft_in;
static fftout_t g_fft_out;
static peak3_t g_peaks;

/* 采样率参数见 app_config.h: ADC_TIM_CLOCK / ADC_MAX_RATE */

/* ================================================================ */

void ADC_Task_Init(void) {
  g_adc_dma_done = 0;
}

/**
 * @brief  启动单缓冲 ADC DMA 采集
 *
 * DMA 从 GPIOC->IDR 采集 FFT_N + 4 点，前 4 点不会传入 FFT。
 * 单帧采集完成后 DMA 自动停止（DMA_NORMAL），触发 AD9220 完成回调。
 */
void ADC_Task_Start(void) {
  AD9220_Start_DMA(g_adc_buffer, FFT_N + AD9220_SETTLING_SAMPLES);
}

void ADC_Task_Stop(void) {
  AD9220_Stop_DMA();
}

void ADC_Task_SetSpeed(Wave_Struct *wave) {
  AD9220_Stop_DMA();

  uint32_t rate = (uint32_t)(wave->carrier_freq) * 100;
  if (rate > ADC_MAX_RATE)
    rate = ADC_MAX_RATE;
  if (rate == 0)
    rate = 1000;

  uint32_t arr = ADC_TIM_CLOCK / rate - 1;
  __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (arr + 1U) / 2U);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, ((arr + 1U) * 2U) / 3U);
  HAL_Delay(1);
}

float32_t ADC_Task_RFFT(uint16_t *pAdcBuffer, float32_t *pBuffer,
                        float32_t *pDst, uint32_t blockSize) {
  arm_rfft_fast_instance_f32 S;
  for (uint32_t i = 0; i < blockSize; i++) {
    pDst[i] = (float32_t)(pAdcBuffer[i + AD9220_SETTLING_SAMPLES]
                          & AD9220_CODE_MASK);
  }
  if (arm_rfft_fast_init_f32(&S, (uint16_t)blockSize) != ARM_MATH_SUCCESS) {
    return 0.0f;
  }
  arm_rfft_fast_f32(&S, pDst, pBuffer, 0);
  for(uint32_t i=0;i<blockSize;i++)
    pDst[i] = 0.0f;
  arm_cmplx_mag_f32(pBuffer, pDst, FFT_N / 2);
  float32_t intergration_val = get_inband_integration(
      pDst, FREQ_START, FREQ_END, blockSize / 2, FREQ_S, FFT_N);
  return intergration_val;
}
