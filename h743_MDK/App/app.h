/**
 * @file    app.h
 * @brief   App 层统一入口 —— 只需 #include "app.h" 即可使用全部应用模块
 */

#ifndef __APP_H
#define __APP_H

#include "app_config.h"
#include "app_types.h"
#include "freq_measure.h"
#include "adc_task.h"
#include "serial.h"
#include "ad9959.h"
#include "ad9910.h"
#include "scheduler.h"
#include "tasks.h"
#include "dsp_filter.h"
#include "dsp_adaptive.h"
#include "dsp_analyze.h"

#endif
