#include "can.h"

#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xil_printf.h"

#ifndef FW_DEBUG
#define FW_DEBUG 0
#endif
#include "xstatus.h"
#include "xtime_l.h"

#define SIM_CAN_BIT_US               50U
#define SIM_CAN_IDLE_READY_US        20000U

#define SIM_CAN_SYNC                 0xA5U
#define SIM_CAN_TARGET               0x20U
#define SIM_CAN_CMD_SET              0xA3U
#define SIM_CAN_GAIN_LEVEL_MIN       1U
#define SIM_CAN_GAIN_LEVEL_MAX       10U

#define SLCR_UNLOCK_ADDR             0xF8000008U
#define SLCR_LOCK_ADDR               0xF8000004U
#define SLCR_UNLOCK_KEY              0x0000DF0DU
#define SLCR_LOCK_KEY                0x0000767BU
#define SLCR_MIO_PIN_09_ADDR         0xF8000724U
#define SLCR_MIO_GPIO_OUT_LVCMOS     0x00000600U

static XGpioPs *sim_can_gpio;
static int sim_can_ready;
static XTime last_tx_end;
/* Debug snapshot kept outside can_send_gain_level() so Vitis can inspect it
 * even when local arrays are optimized out. */
volatile uint8_t sim_can_debug_frame[6];
volatile u16 sim_can_debug_level;
volatile u32 sim_can_debug_tx_started;
volatile u32 sim_can_debug_tx_done;
volatile u32 sim_can_debug_tx_timeout;

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

int can_init(XGpioPs *gpio)
{
    if (gpio == NULL) {
        return XST_INVALID_PARAM;
    }

    sim_can_gpio = gpio;
    sim_can_ready = 0;

    /* MIO9 is the only simulated-CAN signal: GPIO output to the gain board. */
    Xil_Out32(SLCR_UNLOCK_ADDR, SLCR_UNLOCK_KEY);
    Xil_Out32(SLCR_MIO_PIN_09_ADDR, SLCR_MIO_GPIO_OUT_LVCMOS);
    Xil_Out32(SLCR_LOCK_ADDR, SLCR_LOCK_KEY);

    XGpioPs_SetDirectionPin(sim_can_gpio, SIM_CAN_TX_MIO, 1U);
    XGpioPs_SetOutputEnablePin(sim_can_gpio, SIM_CAN_TX_MIO, 1U);
    sim_can_write_line(1U);
    sim_can_delay_us(SIM_CAN_IDLE_READY_US);
    sim_can_ready = 1;
#if FW_DEBUG
    xil_printf("SIM CAN ready: tx=MIO%u rx=disabled bit=%uus idle=high\r\n",
               (unsigned)SIM_CAN_TX_MIO,
               (unsigned)SIM_CAN_BIT_US);
#endif
    return XST_SUCCESS;
}

int can_send_gain_level(u16 level)
{
    uint8_t frame[6];
    uint32_t i;
    XTime now;

    sim_can_debug_tx_started = 0U;
    sim_can_debug_tx_done = 0U;
    sim_can_debug_tx_timeout = 0U;

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
    sim_can_debug_level = level;
    memcpy((void *)sim_can_debug_frame, frame, sizeof(frame));

    /* Gain-only link: do not read or wait for a return GPIO. */
    if (last_tx_end != 0U) {
        do {
            XTime_GetTime(&now);
        } while ((uint64_t)(now - last_tx_end) <
                 ticks_from_us(SIM_CAN_IDLE_READY_US));
    }

    /* Idle, start-low and start-delimiter timing expected by the PA11 receiver. */
    sim_can_debug_tx_started = 1U;
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
    sim_can_debug_tx_done = 1U;

#if FW_DEBUG
    xil_printf("SIM CAN tx level=%u frame=%02x %02x %02x %02x %02x %02x\r\n",
               (unsigned)level,
               frame[0], frame[1], frame[2],
               frame[3], frame[4], frame[5]);
#endif
    return XST_SUCCESS;
}
