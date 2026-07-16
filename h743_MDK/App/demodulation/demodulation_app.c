#include "adc_task.h"
#include "app_config.h"
#include "app_types.h"
#include "arm_math_types.h"
#include "dc_blocker_biquad.h"
#include "dsp/filtering_functions.h"
#include "demodulation_app.h"
__attribute__((section(".AXI_SRAM"))) float32_t adc_buffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t buffer1[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t buffer2[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t Ibuffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t Qbuffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t envelope_buffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t freq_buffer[FFT_N];
DemodulationData do_demodulation(void){
    for(uint32_t i=0;i<FFT_N;i++){
        adc_buffer[i] = (float32_t)g_adc_buffer[i];
    }
    DCBlocker_Init();
    arm_biquad_cascade_df2T_f32(&g_dc_blocker, adc_buffer, adc_buffer, FFT_N);
    fir_lpf_100k_init();
    get_iq(adc_buffer, Ibuffer, Qbuffer, buffer1, FFT_N, CARRIER_FREQ, FREQ_S);
    get_envelope(Ibuffer, Qbuffer, envelope_buffer, FFT_N);
    get_delta_f(Ibuffer, Qbuffer, FFT_N, FREQ_S, freq_buffer);
    ModType_t modulation_type = determine_modulation_method(envelope_buffer, freq_buffer, ENV_CV_GATE, FREQ_CV_GATE, FFT_N);
    DemodulationData out = demodulation(envelope_buffer, freq_buffer, buffer1, buffer2, FREQ_S, modulation_type, FFT_N);
    return out;
}