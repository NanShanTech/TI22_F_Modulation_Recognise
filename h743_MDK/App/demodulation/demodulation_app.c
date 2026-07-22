#include "adc_task.h"
#include "app_config.h"
#include "demodulation_app.h"
#include "arm_math.h"
#include "ad9220.h"
#include "arm_math_types.h"
#include "dsp/statistics_functions.h"
#include <stdint.h>
__attribute__((section(".AXI_SRAM"))) float32_t adc_buffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t buffer1[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t Ibuffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t Qbuffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t envelope_buffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t freq_buffer[FFT_N];
__attribute__((section(".AXI_SRAM"))) float32_t buffer_down[FFT_N_DOWN];
__attribute__((section(".AXI_SRAM"))) float32_t envelope_buffer_down[FFT_N_DOWN_2];
__attribute__((section(".AXI_SRAM"))) float32_t freq_buffer_down[FFT_N];

Wave_Struct do_demodulation(void){
    for(uint32_t i=0;i<FFT_N;i++){
        adc_buffer[i] = (float32_t)(g_adc_buffer[i + AD9220_SETTLING_SAMPLES]
                                    & AD9220_CODE_MASK);
        adc_buffer[i] -= 2048.0f;
    }
    float32_t adc_buffer_mean, adc_buffer_std;
    arm_mean_f32(adc_buffer, FFT_N, &adc_buffer_mean);
    arm_std_f32(adc_buffer, FFT_N, &adc_buffer_std);
    for(uint32_t i=0;i<FFT_N;i++){
        adc_buffer[i] = (adc_buffer[i] - adc_buffer_mean) / adc_buffer_std;
    }
    //DCBlocker_Init();
    //arm_biquad_cascade_df2T_f32(&g_dc_blocker, adc_buffer, adc_buffer, FFT_N);
    decimator_128k_init();
    fir_lpf_100k_init();
    get_iq(adc_buffer, Ibuffer, Qbuffer, buffer1, FFT_N, CARRIER_FREQ, FREQ_S);
    get_envelope(Ibuffer, Qbuffer, envelope_buffer, FFT_N);
    get_delta_f(Ibuffer, Qbuffer, FFT_N, FREQ_S, freq_buffer);
    decimator_128k_process(envelope_buffer, buffer_down);
    for(uint32_t i=0;i<FFT_N_DOWN_2;i++)
        envelope_buffer_down[i] = buffer_down[i + FFT_N_DOWN_BIAS];
    decimator_128k_process(freq_buffer, buffer_down);
    for(uint32_t i=0;i<FFT_N_DOWN_2;i++)
        freq_buffer_down[i] = buffer_down[i + FFT_N_DOWN_BIAS];
    float32_t env_mean, freq_mean;
    arm_mean_f32(envelope_buffer_down, FFT_N_DOWN_2, &env_mean);
    arm_mean_f32(freq_buffer_down, FFT_N_DOWN_2, &freq_mean);
    for(uint32_t i=0; i<FFT_N_DOWN_2;i++){
        //envelope_buffer_down[i] = (envelope_buffer_down[i] - env_mean) / env_mean;
        freq_buffer_down[i] = freq_buffer_down[i] - freq_mean;
    }
    DemodCandidate am_demod = coherence_demodulation(envelope_buffer_down, MOD_FREQ_START, MOD_FREQ_END, MOD_FREQ_STEP, (uint32_t)FFT_N_DOWN_2, FREQ_S_DOWN);
    DemodCandidate fm_demod = coherence_demodulation(freq_buffer_down, MOD_FREQ_START, MOD_FREQ_END, MOD_FREQ_STEP, (uint32_t)FFT_N_DOWN_2, FREQ_S_DOWN);
    Wave_Struct out = determine_modulation_method_coherence(am_demod, fm_demod);
    return out; 
}
