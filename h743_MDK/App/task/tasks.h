/**
 * @file    tasks.h
 * @brief   应用任务调度入口 — 全局状态 + 初始化 + 主处理流程
 *
 *   所有调度器任务槽位（Task_1ms / Task_10ms / Task_100ms / Task_1sec / Task_1min）
 *   的 __weak 覆盖集中在 tasks.c，main.c 只需初始化后调用 Scheduler_Run()。
 */

#ifndef __TASKS_H
#define __TASKS_H

#include "app_types.h"
#include "freq_measure.h"
#include "serial.h"
#include "arm_math_types.h"

/* ---- 全局应用状态 ---- */
extern Wave_Struct   g_wave_info;
extern FreqMeasure   g_freq_measure;
extern HMI_Comm      g_hmi;
extern DacAwgState_t g_dac_awg;
extern uint8_t ddc_notdone_flag;
extern float32_t buffer[FFT_N];
extern float32_t fft_buffer[FFT_N];
extern float32_t intergration_buffer[50];
extern float32_t freq_lo;
/* ---- 应用初始化（main.c 在外设初始化后调用）---- */
void Tasks_Init(UART_HandleTypeDef *huart_hmi);

/* ---- 应用主处理（一帧完整流程：停ADC→FFT→重开ADC）---- */
void App_process(void);

#endif
