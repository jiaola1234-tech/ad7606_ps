#include "can.h"

#include <stdint.h>

#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtime_l.h"

#define SIM_CAN_TX_MIO               9U
#define SIM_CAN_BIT_US               50U
#define SIM_CAN_IDLE_READY_US        20000U

#define SIM_CAN_SYNC                 0xA5U
#define SIM_CAN_TARGET               0x20U
#define SIM_CAN_CMD_SET              0xA3U
#define SIM_CAN_GAIN_LEVEL_MIN       1U
#define SIM_CAN_GAIN_LEVEL_MAX       4U

#define SLCR_UNLOCK_ADDR             0xF8000008U
#define SLCR_LOCK_ADDR               0xF8000004U
#define SLCR_UNLOCK_KEY              0x0000DF0DU
#define SLCR_LOCK_KEY                0x0000767BU
#define SLCR_MIO_PIN_09_ADDR         0xF8000724U
#define SLCR_MIO_GPIO_OUT_LVCMOS     0x00000600U

static XGpioPs *sim_can_gpio;
static int sim_can_ready;

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

    /* Force MIO9 to GPIO output, matching the validated reference program. */
    Xil_Out32(SLCR_UNLOCK_ADDR, SLCR_UNLOCK_KEY);
    Xil_Out32(SLCR_MIO_PIN_09_ADDR, SLCR_MIO_GPIO_OUT_LVCMOS);
    Xil_Out32(SLCR_LOCK_ADDR, SLCR_LOCK_KEY);

    XGpioPs_SetDirectionPin(sim_can_gpio, SIM_CAN_TX_MIO, 1U);
    XGpioPs_SetOutputEnablePin(sim_can_gpio, SIM_CAN_TX_MIO, 1U);
    sim_can_write_line(1U);
    sim_can_delay_us(SIM_CAN_IDLE_READY_US);

    sim_can_ready = 1;
    xil_printf("SIM CAN ready: tx=MIO%u bit=%uus idle=high\r\n",
               (unsigned)SIM_CAN_TX_MIO,
               (unsigned)SIM_CAN_BIT_US);
    return XST_SUCCESS;
}

int can_send_gain_level(u16 level)
{
    uint8_t frame[6];
    uint32_t i;

    if (sim_can_ready == 0 || sim_can_gpio == NULL) {
        xil_printf("SIM CAN tx rejected: not initialized\r\n");
        return XST_FAILURE;
    }
    if (level < SIM_CAN_GAIN_LEVEL_MIN || level > SIM_CAN_GAIN_LEVEL_MAX) {
        xil_printf("SIM CAN tx rejected: bad gain level=%u\r\n",
                   (unsigned)level);
        return XST_INVALID_PARAM;
    }

    frame[0] = SIM_CAN_SYNC;
    frame[1] = SIM_CAN_TARGET;
    frame[2] = SIM_CAN_CMD_SET;
    frame[3] = (uint8_t)level;
    frame[4] = (uint8_t)~frame[3];
    frame[5] = sim_can_crc8(frame, 5U);

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

    xil_printf("SIM CAN tx level=%u frame=%02x %02x %02x %02x %02x %02x\r\n",
               (unsigned)level,
               frame[0], frame[1], frame[2],
               frame[3], frame[4], frame[5]);
    return XST_SUCCESS;
}
