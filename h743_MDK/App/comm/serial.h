/**
 * @file    serial.h
 * @brief   统一串口通信模块
 *
 * 物理通道：
 *   USART1 (PB14/PB15, 921600) — 与电脑交互
 *   USART3 (PB10/PB11, 9600)   — 与串口屏交互 HMI 协议 + 指令接收
 *
 * 分区：
 *   一、通用传输 —— 原始发送 / 格式化发送 / 错误恢复
 *   二、中断接收 —— 空闲中断 + 环形缓冲
 *   三、HMI 协议 —— 串口屏控件操作
 */

#ifndef __SERIAL_H
#define __SERIAL_H

#include "stm32h7xx_hal.h"
#include "app_types.h"

/* ================================================================
 * 一、通用 UART 传输
 * ================================================================ */

void Serial_Recover(UART_HandleTypeDef *huart);
void Serial_Send(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len, uint32_t timeout);
void Serial_Printf(UART_HandleTypeDef *huart, uint32_t timeout, const char *fmt, ...);

/* ================================================================
 * 二、UART 中断接收（空闲中断）
 * ================================================================ */

#define RX_BUF_SIZE  500

void Serial_RxInit(UART_HandleTypeDef *huart);
void Serial_RxOnIdle(UART_HandleTypeDef *huart);

/* ================================================================
 * 三、HMI 串口屏协议
 *
 *   格式: 控件名="值"\xff\xff\xff
 *   通道: USART3, 9600 baud
 * ================================================================ */

typedef struct {
    UART_HandleTypeDef *huart;
} HMI_Comm;

#define HMI_TAIL  "\xff\xff\xff"

void HMI_Init(HMI_Comm *self, UART_HandleTypeDef *huart);

void HMI_SendStr(HMI_Comm *self, const char *ctl, const char *text);
void HMI_SendInt(HMI_Comm *self, const char *ctl, int num);
void HMI_SendFloat(HMI_Comm *self, const char *ctl, float num, int decimals);

void HMI_WaveAdd(HMI_Comm *self, const char *ctl, int ch, int val);
void HMI_WaveClear(HMI_Comm *self, const char *ctl, int ch);

void HMI_SendInitScreen(HMI_Comm *self);
void HMI_ReportWave(HMI_Comm *self, Wave_Struct *wave);
void Serial_ReportFreq(Wave_Struct *wave);

#endif
