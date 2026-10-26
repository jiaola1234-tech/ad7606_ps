#include "external_flow.h"

#include <stdint.h>

#include "xil_exception.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xttcps.h"

#define EXT_FLOW_INPUT_MIO          14U
#define EXT_FLOW_PULSES_PER_LITER   600.0f
#define EXT_FLOW_MIN_INTERVAL_US    200U
#define EXT_FLOW_TIMER_TICK_US      1000U
#define EXT_FLOW_TIMER_PERIOD_US    1024000ULL
#define EXT_FLOW_TIMER_TICK_FREQ_HZ (1000000U / EXT_FLOW_TIMER_TICK_US)
#define EXT_FLOW_TICKS_PER_UPDATE   ((u32)((EXT_FLOW_TIMER_PERIOD_US + EXT_FLOW_TIMER_TICK_US - 1ULL) / EXT_FLOW_TIMER_TICK_US))
#define EXT_FLOW_CAL_MIN_RAW_LITER  0.05f

#define SLCR_UNLOCK_ADDR            0xF8000008U
#define SLCR_LOCK_ADDR              0xF8000004U
#define SLCR_UNLOCK_KEY             0x0000DF0DU
#define SLCR_LOCK_KEY               0x0000767BU
#define SLCR_MIO_PIN_14_ADDR        0xF8000738U
#define SLCR_MIO_GPIO_LVCMOS        0x00000600U

#if defined(XPAR_XGPIOPS_0_INTR)
#define GPIO_INTERRUPT_ID XPAR_XGPIOPS_0_INTR
#elif defined(XPAR_PS7_GPIO_0_INTR)
#define GPIO_INTERRUPT_ID XPAR_PS7_GPIO_0_INTR
#else
#define GPIO_INTERRUPT_ID 52U
#endif

#if defined(XPAR_XTTCPS_0_INTR)
#define TTC_INTERRUPT_ID XPAR_XTTCPS_0_INTR
#else
#define TTC_INTERRUPT_ID 42U
#endif

static XGpioPs *input_gpio;
static XTtcPs timer;
static volatile u32 pulse_total;
static volatile u32 pulse_last_calculation;
static volatile u32 rejected_pulses;
static volatile u32 timer_updates;
static volatile u32 timer_subticks;
static volatile uint64_t last_edge_us;
static volatile uint64_t last_calculation_us;
static volatile uint64_t raw_last_integrate_us;
static volatile float standard_flow_l_min;
static volatile float standard_total_liter;
static volatile float raw_rate_m3s;
static volatile float raw_total_liter;
static volatile float calibration_factor;
static volatile int raw_rate_valid;

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

static void configure_mio14_as_gpio(void)
{
    Xil_Out32(SLCR_UNLOCK_ADDR, SLCR_UNLOCK_KEY);
    Xil_Out32(SLCR_MIO_PIN_14_ADDR, SLCR_MIO_GPIO_LVCMOS);
    Xil_Out32(SLCR_LOCK_ADDR, SLCR_LOCK_KEY);
}

static void integrate_raw_to_now(void)
{
    uint64_t now_us = get_time_us();

    if (raw_last_integrate_us == 0U) {
        raw_last_integrate_us = now_us;
        return;
    }
    if (raw_rate_valid && raw_rate_m3s > 0.0f) {
        uint64_t elapsed_us = now_us - raw_last_integrate_us;
        double elapsed_s = (double)elapsed_us / 1000000.0;
        raw_total_liter += (float)((double)raw_rate_m3s * 1000.0 * elapsed_s);
    }
    raw_last_integrate_us = now_us;
}

static void gpio_interrupt_callback(void *callback, u32 bank, u32 status)
{
    uint64_t now_us;

    (void)callback;
    if (bank != 0U || (status & (1U << EXT_FLOW_INPUT_MIO)) == 0U) {
        return;
    }

    now_us = get_time_us();
    if (last_edge_us != 0U && now_us - last_edge_us < EXT_FLOW_MIN_INTERVAL_US) {
        rejected_pulses++;
        return;
    }
    last_edge_us = now_us;
    pulse_total++;
}

static void timer_interrupt_handler(void *callback)
{
    XTtcPs *instance = (XTtcPs *)callback;
    u32 status = XTtcPs_GetInterruptStatus(instance);

    XTtcPs_ClearInterruptStatus(instance, status);
    if ((status & XTTCPS_IXR_INTERVAL_MASK) != 0U) {
        timer_subticks++;
        if (timer_subticks >= EXT_FLOW_TICKS_PER_UPDATE) {
            timer_subticks = 0U;
            timer_updates++;
        }
    }
}

void external_flow_reset(void)
{
    uint64_t now_us = get_time_us();

    Xil_ExceptionDisable();
    pulse_total = 0U;
    pulse_last_calculation = 0U;
    rejected_pulses = 0U;
    timer_updates = 0U;
    timer_subticks = 0U;
    last_edge_us = 0U;
    last_calculation_us = now_us;
    raw_last_integrate_us = now_us;
    standard_flow_l_min = 0.0f;
    standard_total_liter = 0.0f;
    raw_rate_m3s = 0.0f;
    raw_total_liter = 0.0f;
    calibration_factor = 1.0f;
    raw_rate_valid = 0;
    Xil_ExceptionEnable();
}

void external_flow_set_calibration_factor(float factor)
{
    if (factor <= 0.0f) {
        return;
    }

    /* TODO: The automatic calibration page will own this factor later. */
    Xil_ExceptionDisable();
    calibration_factor = factor;
    Xil_ExceptionEnable();
}

int external_flow_init(XScuGic *interrupt_controller, XGpioPs *gpio)
{
    XTtcPs_Config *timer_config;
    u16 interval;
    u8 prescaler;
    int status;

    if (interrupt_controller == NULL || gpio == NULL) {
        return XST_FAILURE;
    }
    input_gpio = gpio;

    configure_mio14_as_gpio();
    XGpioPs_SetDirectionPin(input_gpio, EXT_FLOW_INPUT_MIO, 0U);
    XGpioPs_SetOutputEnablePin(input_gpio, EXT_FLOW_INPUT_MIO, 0U);
    XGpioPs_IntrDisablePin(input_gpio, EXT_FLOW_INPUT_MIO);
    XGpioPs_IntrClearPin(input_gpio, EXT_FLOW_INPUT_MIO);
    XGpioPs_SetIntrTypePin(input_gpio,
                           EXT_FLOW_INPUT_MIO,
                           XGPIOPS_IRQ_TYPE_EDGE_RISING);
    XGpioPs_SetCallbackHandler(input_gpio, input_gpio, gpio_interrupt_callback);

    status = XScuGic_Connect(interrupt_controller,
                             GPIO_INTERRUPT_ID,
                             (Xil_ExceptionHandler)XGpioPs_IntrHandler,
                             input_gpio);
    if (status != XST_SUCCESS) {
        return status;
    }
    XScuGic_Enable(interrupt_controller, GPIO_INTERRUPT_ID);
    XScuGic_SetPriorityTriggerType(interrupt_controller,
                                  GPIO_INTERRUPT_ID, 0x30U, 0x01U);
    XGpioPs_IntrEnablePin(input_gpio, EXT_FLOW_INPUT_MIO);

    timer_config = XTtcPs_LookupConfig(XPAR_XTTCPS_0_DEVICE_ID);
    if (timer_config == NULL) {
        return XST_FAILURE;
    }
    status = XTtcPs_CfgInitialize(&timer,
                                  timer_config,
                                  timer_config->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    XTtcPs_SetOptions(&timer,
                      XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_WAVE_DISABLE);
    XTtcPs_CalcIntervalFromFreq(&timer,
                                EXT_FLOW_TIMER_TICK_FREQ_HZ,
                                &interval,
                                &prescaler);
    XTtcPs_SetInterval(&timer, interval);
    XTtcPs_SetPrescaler(&timer, prescaler);
    XTtcPs_EnableInterrupts(&timer, XTTCPS_IXR_INTERVAL_MASK);

    status = XScuGic_Connect(interrupt_controller,
                             TTC_INTERRUPT_ID,
                             (Xil_ExceptionHandler)timer_interrupt_handler,
                             &timer);
    if (status != XST_SUCCESS) {
        return status;
    }
    XScuGic_Enable(interrupt_controller, TTC_INTERRUPT_ID);

    external_flow_reset();
    XTtcPs_Start(&timer);
    return XST_SUCCESS;
}

void external_flow_set_raw_rate(float flow_rate_m3s)
{
    Xil_ExceptionDisable();
    integrate_raw_to_now();
    raw_rate_m3s = flow_rate_m3s < 0.0f ? 0.0f : flow_rate_m3s;
    raw_rate_valid = 1;
    Xil_ExceptionEnable();
}

void external_flow_update(void)
{
    uint64_t now_us = get_time_us();
    uint64_t elapsed_us;
    u32 current_total;
    u32 period_pulses;

    Xil_ExceptionDisable();
    if (timer_updates == 0U) {
        Xil_ExceptionEnable();
        return;
    }
    timer_updates = 0U;
    current_total = pulse_total;
    period_pulses = current_total - pulse_last_calculation;
    pulse_last_calculation = current_total;
    Xil_ExceptionEnable();

    if (last_calculation_us == 0U) {
        last_calculation_us = now_us;
        return;
    }
    elapsed_us = now_us - last_calculation_us;
    if (elapsed_us == 0U) {
        return;
    }
    last_calculation_us = now_us;

    standard_total_liter = (float)current_total / EXT_FLOW_PULSES_PER_LITER;
    standard_flow_l_min =
        ((float)period_pulses / EXT_FLOW_PULSES_PER_LITER) *
        (60.0f / ((float)((double)elapsed_us / 1000000.0)));

    integrate_raw_to_now();
    /* Keep the legacy external-flow calibration estimate available in the
     * statistics.  The active velocity calibration still uses the fitted
     * K/B pair in main.c; this ratio is an independent cross-check and does
     * not overwrite the operator's K/B settings. */
    if (raw_total_liter >= EXT_FLOW_CAL_MIN_RAW_LITER &&
        standard_total_liter > 0.0f) {
        calibration_factor = standard_total_liter / raw_total_liter;
    }
}

void external_flow_get_stats(ExternalFlowStats *stats)
{
    if (stats == NULL) {
        return;
    }

    Xil_ExceptionDisable();
    stats->pulse_total = pulse_total;
    stats->rejected_pulses = rejected_pulses;
    stats->standard_flow_l_min = standard_flow_l_min;
    stats->standard_total_liter = standard_total_liter;
    stats->raw_total_liter = raw_total_liter;
    stats->calibration_factor = calibration_factor;
    Xil_ExceptionEnable();
}
