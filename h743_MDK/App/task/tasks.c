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
#include "fft_analyzer.h"
#include "stm32h7xx_hal.h"
#include "tim.h"

/* ---- 全局应用状态 ---- */
Wave_Struct g_wave_info;
FreqMeasure g_freq_measure;
HMI_Comm    g_hmi;


/* ---- 应用初始化 ---- */
void Tasks_Init(UART_HandleTypeDef *huart_hmi)
{
    HMI_Init(&g_hmi, huart_hmi);
    FreqMeasure_Init(&g_freq_measure, &htim2);
//    ADC_Task_Init(&htim3, &hadc1);
}

/* ---- 应用主处理（一帧完整流程：停ADC→FFT→重开ADC）---- */
void App_process(void)
{
    if (!g_adc_dma_done) return;
    g_adc_dma_done = 0;

    ADC_Task_Stop();

    ADC_Task_FFT(&g_wave_info);
   
    ADC_Task_Start();
}

/* 10ms 周期：ADC DMA 帧检测 + FFT 处理链 */

void Task_10ms(uint16_t ticks)
{
    (void)ticks;
    App_process();
}

/* 100ms 周期：HMI 刷新 + 串口频率上报 */
void Task_100ms(void)
{    FreqMeasure_Process(&g_freq_measure, &g_wave_info);
    // HMI_ReportWave(&g_hmi, &g_wave_info);
}

/* 1 秒周期：预留 — 系统心跳 / 统计上报 */
void Task_1sec(void)
{ 
//    Serial_ReportFreq(&g_wave_info);
}

/* 1 分钟周期：预留 — 长时间定时操作 */
void Task_1min(void)
{
}
