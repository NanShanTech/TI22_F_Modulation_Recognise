/**
 * @file    serial.c
 * @brief   统一串口通信模块实现
 */

#include "serial.h"
#include "usart.h"
#include "app_config.h"
#include <stdio.h>
#include <stdarg.h>

/* ================================================================
 * 一、通用 UART 传输（带错误恢复）
 * ================================================================ */

#define TX_TIMEOUT  50

void Serial_Recover(UART_HandleTypeDef *huart)
{
    uint32_t isr = READ_REG(huart->Instance->ISR);

    if (isr & USART_ISR_ORE) SET_BIT(huart->Instance->ICR, USART_ICR_ORECF);
    if (isr & USART_ISR_FE)  SET_BIT(huart->Instance->ICR, USART_ICR_FECF);
    if (isr & USART_ISR_NE)  SET_BIT(huart->Instance->ICR, USART_ICR_NECF);
    if (isr & USART_ISR_PE)  SET_BIT(huart->Instance->ICR, USART_ICR_PECF);

    huart->ErrorCode = HAL_UART_ERROR_NONE;
    huart->gState    = HAL_UART_STATE_READY;
    huart->RxState   = HAL_UART_STATE_READY;
}

void Serial_Send(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len, uint32_t timeout)
{
    if (huart->gState != HAL_UART_STATE_READY)
        Serial_Recover(huart);

    if (HAL_UART_Transmit(huart, (uint8_t *)data, len, timeout) != HAL_OK)
        Serial_Recover(huart);
}

void Serial_Printf(UART_HandleTypeDef *huart, uint32_t timeout, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0) return;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;

    Serial_Send(huart, (uint8_t *)buf, (uint16_t)len, timeout);
}

/* ================================================================
 * 二、UART 中断接收（空闲中断）
 * ================================================================ */

static uint8_t  rx_buf[RX_BUF_SIZE];
static uint16_t rx_len;

void Serial_RxInit(UART_HandleTypeDef *huart)
{
    HAL_UARTEx_ReceiveToIdle_IT(huart, rx_buf, RX_BUF_SIZE);
}

void Serial_RxOnIdle(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) return;

    __disable_irq();

    rx_len = RX_BUF_SIZE - huart->RxXferCount;

    if (rx_len >= 4) {
        for (uint16_t i = 0; i <= (rx_len - 4); i++) {
            if (rx_buf[i] == 0xAA && rx_buf[i + 1] == 0x11) {
                if (rx_buf[i + 6] == 0x55) {
                    break;
                }
            }
        }
    }

    __HAL_UART_CLEAR_IDLEFLAG(huart);
    if (HAL_UART_AbortReceive_IT(huart) == HAL_OK) {
        HAL_UARTEx_ReceiveToIdle_IT(huart, rx_buf, RX_BUF_SIZE);
    }

    __enable_irq();
}

/* ================================================================
 * 三、HMI 串口屏协议
 *
 *   帧格式: "控件名=\"值\"\xff\xff\xff"
 * ================================================================ */

void HMI_Init(HMI_Comm *self, UART_HandleTypeDef *huart)
{
    self->huart = huart;
}

static void HMI_Printf(HMI_Comm *self, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0) return;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;

    // 追加协议尾 \xff\xff\xff
    if (len + 3 <= (int)sizeof(buf)) {
        buf[len + 0] = '\xff';
        buf[len + 1] = '\xff';
        buf[len + 2] = '\xff';
        len += 3;
    }

    uint32_t timeout = __get_PRIMASK() ? 1 : HAL_MAX_DELAY;
    Serial_Send(self->huart, (uint8_t *)buf, (uint16_t)len, timeout);
}

void HMI_SendStr(HMI_Comm *self, const char *ctl, const char *text)
{
    HMI_Printf(self, "%s=\"%s\"", ctl, text);
}

void HMI_SendInt(HMI_Comm *self, const char *ctl, int num)
{
    HMI_Printf(self, "%s=%d", ctl, num);
}

void HMI_SendFloat(HMI_Comm *self, const char *ctl, float num, int decimals)
{
    long scaled = (long)(num * (decimals == 2 ? 100.0f : 1000.0f) + 0.5f);
    HMI_Printf(self, "%s=%ld", ctl, scaled);
}

void HMI_WaveAdd(HMI_Comm *self, const char *ctl, int ch, int val)
{
    HMI_Printf(self, "add %s,%d,%d", ctl, ch, val);
}

void HMI_WaveClear(HMI_Comm *self, const char *ctl, int ch)
{
    HMI_Printf(self, "cle %s,%d", ctl, ch);
}

void HMI_SendInitScreen(HMI_Comm *self)
{
    HMI_SendStr(self, "t4.txt", "unknown");
    HMI_SendStr(self, "t5.txt", "unknown");
    HMI_SendStr(self, "t6.txt", "unknown");
    HMI_SendStr(self, "t7.txt", "unknown");
}

void HMI_ReportWave(HMI_Comm *self, Wave_Struct *wave)
{
    char buf[64];

    /* t4.txt — 调制方式 */
    switch (wave->mod_type) {
        case MOD_FM: HMI_SendStr(self, "t4.txt", "FM"); break;
        case MOD_AM: HMI_SendStr(self, "t4.txt", "AM"); break;
        case MOD_CW: HMI_SendStr(self, "t4.txt", "CW"); break;
    }

    /* t5.txt — 调制度 */
    snprintf(buf, sizeof(buf), "%.1f%%", (double)(wave->mod_depth * 100.0f));
    HMI_SendStr(self, "t5.txt", buf);

    /* t6.txt — 载波频率 */
    if (wave->carrier_freq < 1000.0f) {
        snprintf(buf, sizeof(buf), "%.4f Hz", (double)wave->carrier_freq);
    } else if (wave->carrier_freq < 1000000.0f) {
        snprintf(buf, sizeof(buf), "%.4f KHz", (double)wave->carrier_freq / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%.5f MHz", (double)wave->carrier_freq / 1000000.0);
    }
    HMI_SendStr(self, "t6.txt", buf);

    /* t7.txt — 调制频率 */
    if (wave->mod_freq < 1000.0f) {
        snprintf(buf, sizeof(buf), "%.4f Hz", (double)wave->mod_freq);
    } else if (wave->mod_freq < 1000000.0f) {
        snprintf(buf, sizeof(buf), "%.4f KHz", (double)wave->mod_freq / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%.5f MHz", (double)wave->mod_freq / 1000000.0);
    }
    HMI_SendStr(self, "t7.txt", buf);
}

/* ================================================================
 * 四、VOFA 串口上报（USART1, 921600）
 * ================================================================ */

void Serial_ReportFreq(Wave_Struct *wave)
{
    extern UART_HandleTypeDef huart1;

    if (wave->carrier_freq < 1000.0f) {
        Serial_Printf(&huart1, 50, "Freq: %.5f Hz\r\n", (double)wave->carrier_freq);
    } else if (wave->carrier_freq < 1000000.0f) {
        Serial_Printf(&huart1, 50, "Freq: %.5f KHz\r\n", (double)wave->carrier_freq / 1000.0);
    } else {
        Serial_Printf(&huart1, 50, "Freq: %.5f MHz\r\n", (double)wave->carrier_freq / 1000000.0);
    }
}
