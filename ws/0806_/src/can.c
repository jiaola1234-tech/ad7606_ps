#include "can.h"

#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "xil_exception.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xil_printf.h"

#ifndef FW_DEBUG
#define FW_DEBUG 0
#endif
#include "xstatus.h"
#include "xtime_l.h"
#include "xttcps.h"

#define SIM_CAN_BIT_US               50U
#define SIM_CAN_IDLE_READY_US        20000U

#define SIM_CAN_SYNC                 0xA5U
#define SIM_CAN_TARGET               0x20U
#define SIM_CAN_CMD_SET              0xA3U
#define SIM_CAN_GAIN_LEVEL_MIN       1U
#define SIM_CAN_GAIN_LEVEL_MAX       10U
#define SIM_CAN_CMD_TEMPERATURE      0xB1U
#define SIM_CAN_CMD_TEMP_FAULT       0xB2U
#define SIM_CAN_RX_TIMER_ID          XPAR_XTTCPS_2_DEVICE_ID
#define SIM_CAN_RX_TIMER_INTR        XPAR_XTTCPS_2_INTR

#define SLCR_UNLOCK_ADDR             0xF8000008U
#define SLCR_LOCK_ADDR               0xF8000004U
#define SLCR_UNLOCK_KEY              0x0000DF0DU
#define SLCR_LOCK_KEY                0x0000767BU
#define SLCR_MIO_PIN_09_ADDR         0xF8000724U
#define SLCR_MIO_GPIO_OUT_LVCMOS     0x00000600U

static XGpioPs *sim_can_gpio;
static int sim_can_ready;
static XTtcPs rx_timer;
static u16 rx_bit_interval;
static u16 rx_first_interval;
static volatile u32 rx_state; /* 0 idle, 1 start-low, 2 sampling */
static u32 rx_bit;
static uint8_t rx_frame[6];
static XTime rx_high_since;
static XTime rx_start;
static XTime rx_next_sample;
static XTime last_tx_end;
static volatile XTime temperature_time;
static volatile s16 temperature_value;
static volatile u16 temperature_status = 1U;
static volatile u32 temperature_reports;
static volatile u32 rejected_frames;

static uint64_t ticks_from_us(uint64_t us)
{
    return us * (uint64_t)COUNTS_PER_SECOND / 1000000ULL;
}

u16 can_gain_total(u16 mode)
{
    static const u16 gains[10] = {1U, 10U, 15U, 30U, 100U,
                                 150U, 225U, 300U, 450U, 900U};
    return mode >= 1U && mode <= 10U ? gains[mode - 1U] : 0U;
}

static void sim_can_delay_us(uint32_t us)
{
    XTime start;
    XTime now;
    const uint64_t ticks =
        ((uint64_t)us * (uint64_t)COUNTS_PER_SECOND) / 1000000ULL;

    XTime_GetTime(&start);
    do {
        XTime_GetTime(&now);
    } while ((uint64_t)(now - start) < ticks);
}

static void sim_can_write_line(uint32_t high)
{
    XGpioPs_WritePin(sim_can_gpio, SIM_CAN_TX_MIO, high != 0U ? 1U : 0U);
}

static uint8_t sim_can_crc8(const uint8_t *data, uint32_t length)
{
    uint8_t crc = 0U;
    uint32_t i;

    for (i = 0U; i < length; ++i) {
        uint32_t bit;

        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1U) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return crc;
}

static void rx_stop(void)
{
    XTtcPs_Stop(&rx_timer);
    rx_state = 0U;
}

static void rx_timer_interrupt(void *callback)
{
    XTtcPs *timer = (XTtcPs *)callback;
    u32 status = XTtcPs_GetInterruptStatus(timer);
    XTime now;
    XTtcPs_ClearInterruptStatus(timer, status);
    if ((status & XTTCPS_IXR_INTERVAL_MASK) == 0U || rx_state != 2U) {
        return;
    }
    XTime_GetTime(&now);
    /* Reject a delayed sample rather than silently decoding the next bit. */
    if (now > rx_next_sample + ticks_from_us(20U)) {
        rejected_frames++;
        rx_stop();
        return;
    }
    if (rx_bit == 0U) {
        /* STM32: 50 us start-low, 75 us high, then data. First sample is
         * 100 us after the rising delimiter, at the centre of data bit 0. */
        XTtcPs_SetInterval(timer, rx_bit_interval);
    }
    if (XGpioPs_ReadPin(sim_can_gpio, SIM_CAN_RX_MIO) != 0U) {
        rx_frame[rx_bit / 8U] |= (uint8_t)(1U << (rx_bit % 8U));
    }
    rx_bit++;
    rx_next_sample += ticks_from_us(SIM_CAN_BIT_US);
    if (rx_bit == 48U) {
        rx_stop();
        if (rx_frame[0] != SIM_CAN_SYNC || rx_frame[1] != SIM_CAN_TARGET ||
            sim_can_crc8(rx_frame, 5U) != rx_frame[5] ||
            (rx_frame[2] != SIM_CAN_CMD_TEMPERATURE &&
             rx_frame[2] != SIM_CAN_CMD_TEMP_FAULT)) {
            rejected_frames++;
            return;
        }
        if (rx_frame[2] == SIM_CAN_CMD_TEMPERATURE) {
            temperature_value = (s16)((u16)rx_frame[3] |
                                      ((u16)rx_frame[4] << 8U));
            temperature_status = 0U;
        } else {
            /* Keep the last temperature but expose the fault immediately. */
            temperature_status = 1U;
        }
        temperature_time = now;
        temperature_reports++;
    }
}

void can_gpio_interrupt(u32 bank, u32 status)
{
    XTime now;
    u32 high;
    if (!sim_can_ready || bank != SIM_CAN_RX_MIO / 32U ||
        (status & (1U << (SIM_CAN_RX_MIO % 32U))) == 0U) {
        return;
    }
    XTime_GetTime(&now);
    high = XGpioPs_ReadPin(sim_can_gpio, SIM_CAN_RX_MIO);
    if (high != 0U) {
        rx_high_since = now;
        if (rx_state == 1U) {
            uint64_t low_ticks = (uint64_t)(now - rx_start);
            if (low_ticks < ticks_from_us(30U) ||
                low_ticks > ticks_from_us(75U)) {
                rx_state = 0U;
                rejected_frames++;
                return;
            }
            memset(rx_frame, 0, sizeof(rx_frame));
            rx_bit = 0U;
            rx_state = 2U;
            XTtcPs_Stop(&rx_timer);
            XTtcPs_SetInterval(&rx_timer, rx_first_interval);
            XTtcPs_ResetCounterValue(&rx_timer);
            XTtcPs_ClearInterruptStatus(&rx_timer,
                                       XTtcPs_GetInterruptStatus(&rx_timer));
            XTime_GetTime(&rx_next_sample);
            rx_next_sample += ticks_from_us(100U);
            XTtcPs_Start(&rx_timer);
        }
    } else if (rx_state == 0U &&
               (uint64_t)(now - rx_high_since) >=
               ticks_from_us(SIM_CAN_IDLE_READY_US)) {
        rx_start = now;
        rx_state = 1U;
    }
}

void can_get_temperature(SimCanTemperature *temperature)
{
    XTime now;
    XTime report_time;
    if (temperature == NULL) {
        return;
    }
    /* Only a short snapshot; do not disable IRQs during Modbus writes. */
    Xil_ExceptionDisable();
    temperature->centi_celsius = temperature_value;
    temperature->status = temperature_status;
    temperature->reports = temperature_reports;
    temperature->rejected_frames = rejected_frames;
    report_time = temperature_time;
    Xil_ExceptionEnable();
    XTime_GetTime(&now);
    if (temperature->reports == 0U ||
        (uint64_t)(now - report_time) >
        ticks_from_us(SIM_CAN_TEMP_TIMEOUT_MS * 1000ULL)) {
        temperature->status = 1U;
    }
}

static void sim_can_send_bit(uint8_t bit)
{
    sim_can_write_line(bit != 0U ? 1U : 0U);
    sim_can_delay_us(SIM_CAN_BIT_US);
}

static void sim_can_send_byte(uint8_t value)
{
    uint32_t bit;

    /* The STM32 simulated receiver samples each byte least-significant bit first. */
    for (bit = 0U; bit < 8U; ++bit) {
        sim_can_send_bit((uint8_t)((value >> bit) & 0x01U));
    }
}

int can_init(XGpioPs *gpio, XScuGic *interrupt_controller)
{
    XTtcPs_Config *config;
    u8 prescaler;
    int status;
    if (gpio == NULL || interrupt_controller == NULL) {
        return XST_INVALID_PARAM;
    }

    sim_can_gpio = gpio;
    sim_can_ready = 0;

    /* Force MIO9 to GPIO output, matching the validated reference program. */
    Xil_Out32(SLCR_UNLOCK_ADDR, SLCR_UNLOCK_KEY);
    Xil_Out32(SLCR_MIO_PIN_09_ADDR, SLCR_MIO_GPIO_OUT_LVCMOS);
    /* RX GPIO with tri-state enabled and pull-up, LVCMOS3.3. */
    Xil_Out32(0xF8000700U + 4U * SIM_CAN_RX_MIO, 0x00001601U);
    Xil_Out32(SLCR_LOCK_ADDR, SLCR_LOCK_KEY);

    XGpioPs_SetDirectionPin(sim_can_gpio, SIM_CAN_TX_MIO, 1U);
    XGpioPs_SetOutputEnablePin(sim_can_gpio, SIM_CAN_TX_MIO, 1U);
    sim_can_write_line(1U);
    sim_can_delay_us(SIM_CAN_IDLE_READY_US);

    config = XTtcPs_LookupConfig(SIM_CAN_RX_TIMER_ID);
    if (config == NULL) {
        return XST_FAILURE;
    }
    status = XTtcPs_CfgInitialize(&rx_timer, config, config->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }
    XTtcPs_Stop(&rx_timer);
    XTtcPs_SetOptions(&rx_timer,
                     XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_WAVE_DISABLE);
    XTtcPs_CalcIntervalFromFreq(&rx_timer, 1000000U / SIM_CAN_BIT_US,
                               &rx_bit_interval, &prescaler);
    if (rx_bit_interval == 0U || prescaler == 0xFFU) {
        return XST_FAILURE;
    }
    rx_first_interval = (u16)(2U * rx_bit_interval);
    XTtcPs_SetPrescaler(&rx_timer, prescaler);
    XTtcPs_EnableInterrupts(&rx_timer, XTTCPS_IXR_INTERVAL_MASK);
    status = XScuGic_Connect(interrupt_controller, SIM_CAN_RX_TIMER_INTR,
                            (Xil_ExceptionHandler)rx_timer_interrupt, &rx_timer);
    if (status != XST_SUCCESS) {
        return status;
    }
    XScuGic_SetPriorityTriggerType(interrupt_controller,
                                  SIM_CAN_RX_TIMER_INTR, 0x20U, 0x01U);
    XScuGic_Enable(interrupt_controller, SIM_CAN_RX_TIMER_INTR);
    XGpioPs_SetDirectionPin(gpio, SIM_CAN_RX_MIO, 0U);
    XGpioPs_SetOutputEnablePin(gpio, SIM_CAN_RX_MIO, 0U);
    XGpioPs_IntrDisablePin(gpio, SIM_CAN_RX_MIO);
    XGpioPs_IntrClearPin(gpio, SIM_CAN_RX_MIO);
    XGpioPs_SetIntrTypePin(gpio, SIM_CAN_RX_MIO, XGPIOPS_IRQ_TYPE_EDGE_BOTH);
    rx_state = 0U;
    temperature_reports = 0U;
    temperature_status = 1U;
    XTime_GetTime(&rx_high_since);
    last_tx_end = 0U;

    sim_can_ready = 1;
    XGpioPs_IntrEnablePin(gpio, SIM_CAN_RX_MIO);
#if FW_DEBUG
    xil_printf("SIM CAN ready: tx=MIO%u rx=MIO%u bit=%uus idle=high\r\n",
               (unsigned)SIM_CAN_TX_MIO,
               (unsigned)SIM_CAN_RX_MIO,
               (unsigned)SIM_CAN_BIT_US);
#endif
    return XST_SUCCESS;
}

int can_send_gain_level(u16 level)
{
    uint8_t frame[6];
    uint32_t i;
    XTime now;
    XTime wait_start;

    if (sim_can_ready == 0 || sim_can_gpio == NULL) {
#if FW_DEBUG
        xil_printf("SIM CAN tx rejected: not initialized\r\n");
#endif
        return XST_FAILURE;
    }
    if (level < SIM_CAN_GAIN_LEVEL_MIN || level > SIM_CAN_GAIN_LEVEL_MAX) {
#if FW_DEBUG
        xil_printf("SIM CAN tx rejected: bad gain level=%u\r\n",
                   (unsigned)level);
#endif
        return XST_INVALID_PARAM;
    }

    frame[0] = SIM_CAN_SYNC;
    frame[1] = SIM_CAN_TARGET;
    frame[2] = SIM_CAN_CMD_SET;
    frame[3] = (uint8_t)level;
    frame[4] = (uint8_t)~frame[3];
    frame[5] = sim_can_crc8(frame, 5U);

    /* STM32 polls PA11 in its foreground loop while temperature TX blocks
     * that loop. Wait for its return line to be idle before issuing a gain. */
    XTime_GetTime(&wait_start);
    do {
        XTime_GetTime(&now);
        if (rx_state == 1U && now - rx_start > ticks_from_us(1000U)) {
            rx_state = 0U;
        }
        if (rx_state == 0U &&
            XGpioPs_ReadPin(sim_can_gpio, SIM_CAN_RX_MIO) != 0U &&
            (now - rx_high_since) >= ticks_from_us(3000U) &&
            (last_tx_end == 0U ||
             now - last_tx_end >= ticks_from_us(SIM_CAN_IDLE_READY_US))) {
            break;
        }
        if (now - wait_start > ticks_from_us(30000U)) {
#if FW_DEBUG
            xil_printf("SIM CAN tx timeout: rx=MIO%u pin=%u state=%u\r\n",
                       (unsigned)SIM_CAN_RX_MIO,
                       (unsigned)XGpioPs_ReadPin(sim_can_gpio,
                                                 SIM_CAN_RX_MIO),
                       (unsigned)rx_state);
#endif
            return XST_FAILURE;
        }
    } while (1);

    /* Idle, start-low and start-delimiter timing expected by the PA11 receiver. */
    sim_can_write_line(1U);
    sim_can_delay_us(SIM_CAN_BIT_US * 4U);

    sim_can_write_line(0U);
    sim_can_delay_us(SIM_CAN_BIT_US * 3U);
    sim_can_write_line(1U);
    sim_can_delay_us(SIM_CAN_BIT_US * 2U);

    for (i = 0U; i < (uint32_t)sizeof(frame); ++i) {
        sim_can_send_byte(frame[i]);
    }

    sim_can_write_line(1U);
    sim_can_delay_us(SIM_CAN_BIT_US * 4U);
    XTime_GetTime(&last_tx_end);

#if FW_DEBUG
    xil_printf("SIM CAN tx level=%u frame=%02x %02x %02x %02x %02x %02x\r\n",
               (unsigned)level,
               frame[0], frame[1], frame[2],
               frame[3], frame[4], frame[5]);
#endif
    return XST_SUCCESS;
}
