#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "xparameters.h"

/* 0: screen/RS485 only; 1: also enable the Ethernet TCP stream. */
#define ENABLE_TCP_STREAM         1

/* Gain command wire: Zynq MIO9 -> STM32 PA11. */
#define SIM_CAN_TX_MIO            9U
#define GAIN_INITIAL_MODE         5U
#define GAIN_SETTLE_MS            100U
#define GAIN_AUTO_COOLDOWN_MS     500ULL
#define GAIN_AUTO_RAISE_SAMPLES   (ADC_SAMPLE_RATE_HZ * 2U)
#define GAIN_AUTO_LOW_RMS         (32768.0f * 0.08f)
#define GAIN_AUTO_HIGH_RMS        (32768.0f * 0.32f)
#define GAIN_AUTO_HIGH_PEAK       (32768.0f * 0.88f)
#define GAIN_AUTO_NEXT_RMS_LIMIT  (32768.0f * 0.30f)
#define GAIN_AUTO_NEXT_PEAK_LIMIT (32768.0f * 0.70f)

#define NUM_CHANNELS              8
#define ADC_SAMPLE_RATE_HZ        50000U
#define DECIMATION_FACTOR         25U
#define SAMPLE_RATE_HZ            ((float)ADC_SAMPLE_RATE_HZ / (float)DECIMATION_FACTOR)

/* Four cascaded one-pole sections, fc ~= 300 Hz at 50 kHz. */
#define DECIMATION_FILTER_STAGES  4
#define DECIMATION_FILTER_ALPHA   0.036997f

/*
 * FFT stability A/B profiles:
 *   0 = baseline:       16.384 s big, 1.024 s small, 50% overlap, 31 windows
 *   1 = longer small:   16.384 s big, 2.048 s small, 50% overlap, 15 windows
 *   2 = more overlap:   16.384 s big, 1.024 s small, 75% overlap, 61 windows
 *   3 = longer big:     24.576 s big, 1.024 s small, 50% overlap, 47 windows
 */
#define FLOW_STABILITY_PROFILE    1

#if FLOW_STABILITY_PROFILE == 0
#define FFT_SIZE                  2048
#define FFT_VALID_SAMPLES         2048
#define FFT_OVERLAP_SAMPLES       1024
#define ANALYSIS_WINDOW_SAMPLES_PER_CH 32768
#define NUM_FREQ                  52
#elif FLOW_STABILITY_PROFILE == 1
#define FFT_SIZE                  4096
#define FFT_VALID_SAMPLES         4096
#define FFT_OVERLAP_SAMPLES       2048
#define ANALYSIS_WINDOW_SAMPLES_PER_CH 32768
#define NUM_FREQ                  103
#elif FLOW_STABILITY_PROFILE == 2
#define FFT_SIZE                  2048
#define FFT_VALID_SAMPLES         2048
#define FFT_OVERLAP_SAMPLES       1536
#define ANALYSIS_WINDOW_SAMPLES_PER_CH 32768
#define NUM_FREQ                  52
#elif FLOW_STABILITY_PROFILE == 3
#define FFT_SIZE                  2048
#define FFT_VALID_SAMPLES         2048
#define FFT_OVERLAP_SAMPLES       1024
#define ANALYSIS_WINDOW_SAMPLES_PER_CH 49152
#define NUM_FREQ                  52
#else
#error "Unsupported FLOW_STABILITY_PROFILE"
#endif

#define FFT_STEP_SAMPLES          (FFT_VALID_SAMPLES - FFT_OVERLAP_SAMPLES)
#define DMA_SAMPLES_PER_CH        2048
#define NUM_FFT_BLOCKS            (((ANALYSIS_WINDOW_SAMPLES_PER_CH - FFT_VALID_SAMPLES) / FFT_STEP_SAMPLES) + 1)
#define FLOW_UPDATE_SAMPLES       2048U

#define NUM_K                     401
#define NUM_U                     800
#define NUM_UF                    50
#define MOVMEAN_WINDOW            3
#define COV_CURRENT_WEIGHT        0.40f
#define COV_DIAGONAL_LOADING      1.0e-3f
#define PI                        3.1415926f
#define EPS                       1e-12f
#define PEAK_AVG_RANGE_RATIO      0.30f
#define PEAK_AVG_RANGE_MAX        0.90f

#define U_CAL_A                   0.0f
#define MY_FLOW_CAL_FACTOR        1.0f
#define U_CAL_B                   MY_FLOW_CAL_FACTOR
#define U_CAL_C                   0.0f
#define PIPE_DIAMETER_M           0.080f

#define DMA_BLOCK_SAMPLES         (NUM_CHANNELS * DMA_SAMPLES_PER_CH)
#define RAW_WINDOW_SAMPLES_PER_CH ANALYSIS_WINDOW_SAMPLES_PER_CH
#define RAW_WINDOW_SAMPLES        (NUM_CHANNELS * RAW_WINDOW_SAMPLES_PER_CH)
#define RAW_DMA_BUF_NUM           64
#define ADC_BYTE                  2
#define TCP_BLOCK_BYTES           (DMA_BLOCK_SAMPLES * ADC_BYTE)
#define FREQ_AXIS_MAX             (((NUM_FREQ - 1) * SAMPLE_RATE_HZ) / FFT_SIZE)

#if (ADC_SAMPLE_RATE_HZ % DECIMATION_FACTOR) != 0
#error "ADC sample rate must be divisible by the decimation factor"
#endif

#if FFT_VALID_SAMPLES > FFT_SIZE
#error "FFT valid samples cannot exceed FFT size"
#endif

#if FFT_OVERLAP_SAMPLES >= FFT_VALID_SAMPLES
#error "FFT overlap must be smaller than the valid window"
#endif

#endif
