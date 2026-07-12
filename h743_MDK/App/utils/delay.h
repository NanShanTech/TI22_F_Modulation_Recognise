/**
 * @file    delay.h
 * @brief   微秒 / 毫秒延时（DWT 硬件定时器，不依赖 SysTick）
 */

#ifndef __DELAY_H
#define __DELAY_H

#include "stm32h7xx_hal.h"

void delay_init(void);            // 初始化 DWT 周期计数器
void delay_us(uint32_t us);       // 微秒延时
void delay_ms(uint32_t ms);       // 毫秒延时

#endif
