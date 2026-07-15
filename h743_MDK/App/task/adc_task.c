/**
 * @file    adc_task.c
 * @brief   ADC + DMA 采集与 FFT 任务实现（单缓冲模式）
 *
 *   ADC DMA → g_adc_buffer[0..N-1] → ConvCpltCallback 置 g_adc_dma_done →
 *   主循环检测 g_adc_dma_done → 停 ADC → FFT → 重开 ADC
 */

#include "adc_task.h"
#include "adc.h"
#include "app_config.h"
#include "arm_math_types.h"
#include "ddc.h"
#include "dma.h"
#include "dsp/complex_math_functions.h"
#include "dsp/transform_functions.h"
#include "fft_helper.h"
#include "stm32h7xx_hal.h"
#include "tasks.h"
#include "tim.h"
#include <stdint.h>
#include <string.h>

/* ---- 硬件句柄 ---- */
static TIM_HandleTypeDef *g_htim;
static ADC_HandleTypeDef *g_hadc;

/* ---- DMA 缓冲区（AXI SRAM，MPU 已设为 non-cacheable）---- */
__attribute__((section(".AXI_SRAM"))) uint16_t g_adc_buffer[FFT_N];

/* ---- DMA 完成标志（ISR 写，主循环读）---- */
volatile uint8_t g_adc_dma_done;

/* ---- FFT 工作缓存 ---- */
static fftin_t g_fft_in;
static fftout_t g_fft_out;
static peak3_t g_peaks;

/* 采样率参数见 app_config.h: ADC_TIM_CLOCK / ADC_MAX_RATE */

/* ================================================================ */

void ADC_Task_Init(TIM_HandleTypeDef *htim, ADC_HandleTypeDef *hadc) {
  g_htim = htim;
  g_hadc = hadc;
  g_adc_dma_done = 0;
}

/**
 * @brief  启动单缓冲 ADC DMA 采集
 *
 * HAL_ADC_Start_DMA 内部配置 DMA 流：源=ADC_DR，目标=g_adc_buffer，长度=FFT_N。
 * 单帧采集完成后 DMA 自动停止（DMA_NORMAL），触发 HAL_ADC_ConvCpltCallback。
 */
void ADC_Task_Start(void) {
  HAL_ADC_Start_DMA(g_hadc, (uint32_t *)g_adc_buffer, FFT_N);
  HAL_TIM_Base_Start(g_htim);
}

void ADC_Task_Stop(void) {
  HAL_ADC_Stop_DMA(g_hadc);
  HAL_TIM_Base_Stop(g_htim);
}

void ADC_Task_SetSpeed(Wave_Struct *wave) {
  HAL_TIM_Base_Stop(g_htim);

  uint32_t rate = (uint32_t)(wave->carrier_freq) * 100;
  if (rate > ADC_MAX_RATE)
    rate = ADC_MAX_RATE;
  if (rate == 0)
    rate = 1000;

  uint32_t arr = ADC_TIM_CLOCK / rate - 1;
  __HAL_TIM_SET_AUTORELOAD(g_htim, arr);
  HAL_TIM_Base_Init(g_htim);
  HAL_Delay(1);
}

float32_t ADC_Task_RFFT(uint16_t *adc_buffer, float32_t *buffer,
                        float32_t *pDst, uint32_t blockSize) {
  arm_rfft_fast_instance_f32 S;
  rfft_prepare(adc_buffer, buffer, blockSize);
  arm_rfft_fast_init_4096_f32(&S);
  arm_rfft_fast_f32(&S, buffer, pDst, 0);
  memset(buffer,FFT_N,0.0f);//buffer归零
  arm_cmplx_mag_f32(fft_buffer, buffer, FFT_N / 2);
  float32_t intergration_val = get_inband_integration(
      buffer, FREQ_START, FREQ_END, blockSize / 2, FREQ_S, FFT_N);
  return intergration_val;
}