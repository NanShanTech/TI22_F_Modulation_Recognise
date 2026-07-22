#include "ad9220.h"
#include "tim.h"

extern DMA_HandleTypeDef hdma_tim2_ch2;

static void AD9220_DMA_CpltCallback(DMA_HandleTypeDef *hdma)
{
    if (hdma->Instance != hdma_tim2_ch2.Instance) {
        return;
    }

    AD9220_Stop_DMA();
    AD9220_ConvCpltCallback();
}

__weak void AD9220_ConvCpltCallback(void)
{
}

void AD9220_Start_DMA(uint16_t *adc_buffer, uint32_t buffer_length)
{
    AD9220_Stop_DMA();

    HAL_DMA_RegisterCallback(&hdma_tim2_ch2,
                             HAL_DMA_XFER_CPLT_CB_ID,
                             AD9220_DMA_CpltCallback);

    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_UPDATE);

    HAL_DMA_Start_IT(&hdma_tim2_ch2,
                     (uint32_t)&GPIOC->IDR,
                     (uint32_t)adc_buffer,
                     buffer_length);

    __HAL_TIM_ENABLE_DMA(&htim2, TIM_DMA_CC2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_OC_Start(&htim2, TIM_CHANNEL_2);
}

void AD9220_Stop_DMA(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    HAL_TIM_OC_Stop(&htim2, TIM_CHANNEL_2);
    __HAL_TIM_DISABLE_DMA(&htim2, TIM_DMA_CC2);
    HAL_DMA_Abort(&hdma_tim2_ch2);
    HAL_DMA_UnRegisterCallback(&hdma_tim2_ch2, HAL_DMA_XFER_CPLT_CB_ID);
    __HAL_TIM_DISABLE(&htim2);
}
