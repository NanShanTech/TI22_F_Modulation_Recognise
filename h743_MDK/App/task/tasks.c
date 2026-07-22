/**
 * @file    tasks.c
 * @brief   任务调度槽位实现 — ADC 采集、FFT、频率测量、HMI 上报
 *
 *   通过覆盖 scheduler.c 中的 __weak 任务函数，将各功能模块
 *   挂载到时间触发调度器的对应槽位。
 *
 *   时间链：1ms → 10ms → 100ms → 1秒 → 1分钟
 *     Task_10ms:  ADC DMA 帧检测 + FFT 处理链（核心实时路径）
 *     Task_100ms: 预留 — HMI 刷新 / 低速外设轮询
 *     Task_1sec:  预留 — 系统心跳 / 统计上报
 *     Task_1min:  预留 — 长时间定时操作
 */

#include "tasks.h"
#include "adc_task.h"
#include "app_config.h"
#include "arm_math_types.h"
#include "dsp/statistics_functions.h"
#include "stm32h7xx_hal.h"
#include "tim.h"
#include "ad9910.h"
#include "ddc.h"

/* ---- 全局应用状态 ---- */
Wave_Struct g_wave_info;
FreqMeasure g_freq_measure;
HMI_Comm    g_hmi;
uint8_t ddc_notdone_flag = 1;
__attribute__((section(".AXI_SRAM"))) float32_t mag_buffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t fft_buffer[FFT_N];
float32_t intergration_buffer[50];
uint32_t intergration_read_index = 0;
float32_t freq_lo;
/* ---- 应用初始化 ---- */
void Tasks_Init(UART_HandleTypeDef *huart_hmi)
{
}

/* ---- AD9910 扫频状态 ---- */
static float g_sweep_freq_hz = 9800000.0f; 


/* ---- 应用主处理（一帧完整流程：停ADC→FFT或者其他流程→扫频→重开ADC）---- */
void App_process(void)
{
    g_adc_dma_done = 0;
    ADC_Task_Stop();
   float32_t intergration =  ADC_Task_RFFT(g_adc_buffer, mag_buffer, fft_buffer, FFT_N);
    if (g_sweep_freq_hz == 9800000.0f)
        intergration_read_index = 0;
    intergration_buffer[intergration_read_index] = intergration;
    intergration_read_index += 1;
    //ADC_Task_FFT(&g_wave_info);
    if (g_sweep_freq_hz > 29800000.0f) {
			uint32_t freq_index;
            arm_max_f32(intergration_buffer, 50, &freq_lo, &freq_index);
            if (freq_index > 0)
                freq_index -= 1;
            freq_index *= 500e3;
            freq_index += 9.8e6;
            freq_lo = freq_index;
            AD9910_FreWrite((double)freq_index);
            AD9910_AmpWrite(20);
            ddc_notdone_flag = 0;
            g_sweep_freq_hz = 9800000.0f;
            return;
			
    } else {
        ddc_notdone_flag = 1;
        AD9910_FreWrite((double)g_sweep_freq_hz);
        AD9910_AmpWrite(20);
        g_sweep_freq_hz += 500000.0f;      
        ADC_Task_Start();//重开 ADC 
        HAL_Delay(100);
    }
}

/* 10ms 周期 */
void Task_10ms(uint16_t ticks)
{
    (void)ticks;
}

/* 100ms 周期：*/
void Task_100ms(void)
{
//    FreqMeasure_Process(&g_freq_measure, &g_wave_info);
}

/* 1 秒周期：*/
void Task_1sec(void)
{
    HMI_ReportWave(&g_hmi, &g_wave_info);  /* 同时发给 HMI(USART3) 和 VOFA(USART1) */
}

/* 1 分钟周期：预留 — 长时间定时操作 */
void Task_1min(void)
{
}
