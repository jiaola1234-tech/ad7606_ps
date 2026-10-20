#include "flow_pulse.h"

#include <stdint.h>

#include "xil_exception.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xttcps.h"

#define FLOW_PULSE_MIO       15U
#define FLOW_DEFAULT_PULSE_VOLUME_LITER 1.0f
#define FLOW_DEFAULT_PULSE_COUNT 1U
#define FLOW_DEFAULT_PULSE_WIDTH_US 20000U
#define FLOW_MAX_ACC_LITER   10.0
#define FLOW_MIN_DELAY_US    100U
#define FLOW_TTC_DEVICE_ID    XPAR_XTTCPS_1_DEVICE_ID
#define FLOW_TTC_INTERRUPT_ID XPAR_XTTCPS_1_INTR
#define FLOW_TTC_MAX_INTERVAL 0xFFFFU

typedef enum {
    FLOW_EVENT_NONE = 0,
    FLOW_EVENT_NEXT_PULSE_START,
    FLOW_EVENT_PULSE_END
} FlowPulseEvent;

static XTtcPs timer;
static XGpioPs *pulse_gpio;
static volatile float current_rate_m3s;
static volatile float total_volume_m3;
static volatile double accumulated_liter;
static volatile double pulse_liter_per_count;
static volatile float pulse_volume_liter;
static volatile u16 pulse_count_setting;
static volatile u32 pulse_width_us;
static volatile int rate_valid;
static volatile int pulse_active;
static volatile int overflow;
static volatile uint64_t last_integrate_us;
static volatile FlowPulseEvent current_event;

static uint64_t get_time_us(void)
{
    XTime now;
    uint64_t counts;
    uint64_t seconds;
    uint64_t remainder;

    XTime_GetTime(&now);
    counts = (uint64_t)now;
    seconds = counts / (uint64_t)COUNTS_PER_SECOND;
    remainder = counts % (uint64_t)COUNTS_PER_SECOND;
    return seconds * 1000000ULL +
           remainder * 1000000ULL / (uint64_t)COUNTS_PER_SECOND;
}

static void drive_high(void)
{
    XGpioPs_WritePin(pulse_gpio, FLOW_PULSE_MIO, 1U);
}

static void drive_low(void)
{
    XGpioPs_WritePin(pulse_gpio, FLOW_PULSE_MIO, 0U);
}

static void integrate_to_now(void)
{
    uint64_t now_us = get_time_us();

    if (last_integrate_us == 0U) {
        last_integrate_us = now_us;
        return;
    }

    if (rate_valid) {
        uint64_t elapsed_us = now_us - last_integrate_us;
        double elapsed_s = (double)elapsed_us / 1000000.0;
        double max_accumulated_liter;

        total_volume_m3 += (float)((double)current_rate_m3s * elapsed_s);
        accumulated_liter += (double)current_rate_m3s * 1000.0 * elapsed_s;
        max_accumulated_liter = pulse_liter_per_count + FLOW_MAX_ACC_LITER;
        if (max_accumulated_liter < FLOW_MAX_ACC_LITER) {
            max_accumulated_liter = FLOW_MAX_ACC_LITER;
        }
        if (accumulated_liter > max_accumulated_liter) {
            accumulated_liter = max_accumulated_liter;
            overflow = 1;
        }
    }
    last_integrate_us = now_us;
}

static void stop_event(void)
{
    u32 status;

    XTtcPs_Stop(&timer);
    status = XTtcPs_GetInterruptStatus(&timer);
    XTtcPs_ClearInterruptStatus(&timer, status);
    current_event = FLOW_EVENT_NONE;
}

static void start_one_shot_us(uint32_t delay_us)
{
    uint64_t numerator;
    uint64_t divisor;
    uint64_t denominator;
    uint64_t counts;
    u8 prescaler;
    u32 status;

    if (delay_us < FLOW_MIN_DELAY_US) {
        delay_us = FLOW_MIN_DELAY_US;
    }

    numerator = (uint64_t)timer.Config.InputClockHz * delay_us;
    prescaler = XTTCPS_CLK_CNTRL_PS_DISABLE;
    denominator = 1000000ULL;
    counts = (numerator + denominator - 1ULL) / denominator;

    if (counts > FLOW_TTC_MAX_INTERVAL) {
        for (prescaler = 0U; prescaler < XTTCPS_CLK_CNTRL_PS_DISABLE; ++prescaler) {
            divisor = 1ULL << (prescaler + 1U);
            denominator = 1000000ULL * divisor;
            counts = (numerator + denominator - 1ULL) / denominator;
            if (counts <= FLOW_TTC_MAX_INTERVAL) {
                break;
            }
        }
    }
    if (prescaler >= XTTCPS_CLK_CNTRL_PS_DISABLE &&
        counts > FLOW_TTC_MAX_INTERVAL) {
        prescaler = 15U;
        counts = FLOW_TTC_MAX_INTERVAL;
    }
    if (counts < 1ULL) {
        counts = 1ULL;
    }

    XTtcPs_Stop(&timer);
    XTtcPs_SetPrescaler(&timer, prescaler);
    XTtcPs_SetInterval(&timer, (XInterval)counts);
    XTtcPs_ResetCounterValue(&timer);
    status = XTtcPs_GetInterruptStatus(&timer);
    XTtcPs_ClearInterruptStatus(&timer, status);
    XTtcPs_Start(&timer);
}

static void schedule_next_pulse(void)
{
    double remaining_liter;
    double flow_liter_per_second;
    double delay_seconds;
    double pulse_liter = pulse_liter_per_count;
    uint32_t delay_us;

    if (pulse_active) {
        return;
    }
    if (!rate_valid || current_rate_m3s <= 0.0f ||
        pulse_liter <= 0.0) {
        stop_event();
        return;
    }

    flow_liter_per_second = (double)current_rate_m3s * 1000.0;
    if (accumulated_liter >= pulse_liter) {
        current_event = FLOW_EVENT_NEXT_PULSE_START;
        start_one_shot_us(FLOW_MIN_DELAY_US);
        return;
    }

    remaining_liter = pulse_liter - accumulated_liter;
    delay_seconds = remaining_liter / flow_liter_per_second;
    if (delay_seconds < (double)FLOW_MIN_DELAY_US / 1000000.0) {
        delay_us = FLOW_MIN_DELAY_US;
    } else {
        double value = delay_seconds * 1000000.0;
        delay_us = value > 4294967294.0 ? 4294967294U : (uint32_t)value;
    }

    current_event = FLOW_EVENT_NEXT_PULSE_START;
    start_one_shot_us(delay_us);
}

static void timer_interrupt_handler(void *callback)
{
    XTtcPs *instance = (XTtcPs *)callback;
    u32 status = XTtcPs_GetInterruptStatus(instance);
    double pulse_liter = pulse_liter_per_count;

    XTtcPs_Stop(instance);
    XTtcPs_ClearInterruptStatus(instance, status);
    if ((status & XTTCPS_IXR_INTERVAL_MASK) == 0U) {
        return;
    }
    integrate_to_now();

    if (current_event == FLOW_EVENT_NEXT_PULSE_START) {
        if (!pulse_active && pulse_liter > 0.0 &&
            accumulated_liter >= pulse_liter) {
            accumulated_liter -= pulse_liter;
            drive_high();
            pulse_active = 1;
            current_event = FLOW_EVENT_PULSE_END;
            start_one_shot_us(pulse_width_us);
        } else {
            schedule_next_pulse();
        }
        return;
    }

    if (current_event == FLOW_EVENT_PULSE_END) {
        drive_low();
        pulse_active = 0;
        schedule_next_pulse();
    }
}

int flow_pulse_init(XScuGic *interrupt_controller, XGpioPs *gpio)
{
    XTtcPs_Config *config;
    int status;

    if (interrupt_controller == NULL || gpio == NULL) {
        return XST_FAILURE;
    }
    pulse_gpio = gpio;
    XGpioPs_SetDirectionPin(pulse_gpio, FLOW_PULSE_MIO, 1U);
    XGpioPs_SetOutputEnablePin(pulse_gpio, FLOW_PULSE_MIO, 1U);
    drive_low();

    config = XTtcPs_LookupConfig(FLOW_TTC_DEVICE_ID);
    if (config == NULL) {
        return XST_FAILURE;
    }
    status = XTtcPs_CfgInitialize(&timer, config, config->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    XTtcPs_SetOptions(&timer,
                      XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_WAVE_DISABLE);
    XTtcPs_SetPrescaler(&timer, XTTCPS_CLK_CNTRL_PS_DISABLE);
    XTtcPs_SetInterval(&timer, 1U);
    XTtcPs_ResetCounterValue(&timer);
    XTtcPs_EnableInterrupts(&timer, XTTCPS_IXR_INTERVAL_MASK);

    status = XScuGic_Connect(interrupt_controller,
                             FLOW_TTC_INTERRUPT_ID,
                             (Xil_ExceptionHandler)timer_interrupt_handler,
                             &timer);
    if (status != XST_SUCCESS) {
        return status;
    }
    XScuGic_Enable(interrupt_controller, FLOW_TTC_INTERRUPT_ID);

    XTtcPs_Stop(&timer);
    {
        u32 interrupt_status = XTtcPs_GetInterruptStatus(&timer);
        XTtcPs_ClearInterruptStatus(&timer, interrupt_status);
    }

    current_rate_m3s = 0.0f;
    total_volume_m3 = 0.0f;
    accumulated_liter = 0.0;
    pulse_liter_per_count =
        (double)FLOW_DEFAULT_PULSE_VOLUME_LITER /
        (double)FLOW_DEFAULT_PULSE_COUNT;
    pulse_volume_liter = FLOW_DEFAULT_PULSE_VOLUME_LITER;
    pulse_count_setting = FLOW_DEFAULT_PULSE_COUNT;
    pulse_width_us = (u32)FLOW_DEFAULT_PULSE_WIDTH_US;
    rate_valid = 0;
    pulse_active = 0;
    overflow = 0;
    last_integrate_us = get_time_us();
    current_event = FLOW_EVENT_NONE;
    return XST_SUCCESS;
}

void flow_pulse_set_config(float config_volume_liter,
                           u16 config_pulse_count,
                           u16 config_pulse_width_us)
{
    double next_pulse_liter;
    u32 next_pulse_width_us;

    if (config_volume_liter <= 0.0f ||
        config_pulse_count == 0U ||
        config_pulse_width_us == 0U) {
        return;
    }

    next_pulse_liter =
        (double)config_volume_liter / (double)config_pulse_count;
    if (next_pulse_liter <= 0.0) {
        return;
    }
    next_pulse_width_us = (u32)config_pulse_width_us;

    Xil_ExceptionDisable();
    integrate_to_now();
    pulse_volume_liter = config_volume_liter;
    pulse_count_setting = config_pulse_count;
    pulse_liter_per_count = next_pulse_liter;
    pulse_width_us = next_pulse_width_us;
    if (accumulated_liter > pulse_liter_per_count + FLOW_MAX_ACC_LITER) {
        accumulated_liter = pulse_liter_per_count + FLOW_MAX_ACC_LITER;
        overflow = 1;
    }
    if (!pulse_active) {
        schedule_next_pulse();
    }
    Xil_ExceptionEnable();
}

void flow_pulse_set_rate(float flow_rate_m3s)
{
    Xil_ExceptionDisable();
    integrate_to_now();
    current_rate_m3s = flow_rate_m3s < 0.0f ? 0.0f : flow_rate_m3s;
    rate_valid = 1;
    if (!pulse_active) {
        schedule_next_pulse();
    }
    Xil_ExceptionEnable();
}

void flow_pulse_reset_total(void)
{
    Xil_ExceptionDisable();
    integrate_to_now();
    total_volume_m3 = 0.0f;
    Xil_ExceptionEnable();
}

void flow_pulse_abort(void)
{
    Xil_ExceptionDisable();
    stop_event();
    pulse_active = 0;
    rate_valid = 0;
    drive_low();
    Xil_ExceptionEnable();
}

float flow_pulse_total_volume_m3(void)
{
    float result;

    Xil_ExceptionDisable();
    integrate_to_now();
    result = total_volume_m3;
    Xil_ExceptionEnable();
    return result;
}

