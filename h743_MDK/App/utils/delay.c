/**
 * @file    delay.c
 * @brief   DWT 硬件定时器延时实现
 */

#include "delay.h"

static uint32_t g_ticks_per_us;

void delay_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_ticks_per_us = SystemCoreClock / 1000000;
}

void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t target = us * g_ticks_per_us;
    while ((DWT->CYCCNT - start) < target) {}
}

void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        delay_us(1000);
    }
}
