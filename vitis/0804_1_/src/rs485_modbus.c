#include "rs485_modbus.h"

#include <stdint.h>

#include "xparameters.h"
#include "xstatus.h"
#include "xuartps.h"

#define UART_DEVICE_ID              XPAR_XUARTPS_0_DEVICE_ID
#define GPIO_DEVICE_ID              XPAR_XGPIOPS_0_DEVICE_ID
#define RS485_DE_MIO                41U
#define MODBUS_SLAVE_ADDRESS        0x01U
#define MODBUS_READ_HOLDING_REG     0x03U
#define MODBUS_WRITE_SINGLE_REG     0x06U
#define MODBUS_WRITE_MULTI_REG      0x10U
#define MODBUS_EXCEPTION_MASK       0x80U
#define MEASUREMENT_START_REG       0U     /* 4WUB0001 */
#define MEASUREMENT_REG_COUNT       4U
#define RESET_BUTTON_REG            4U     /* 4WUB0005 */
#define RESET_READ_START_REG        RESET_BUTTON_REG
#define RESET_READ_REG_COUNT        1U
#define RESET_BUTTON_INDEX          0U
#define RESET_PRESSED_VALUE         1U
#define CAL_CONTROL_START_REG       99U    /* 4WUB0100 */
#define CAL_CONTROL_REG_COUNT       7U
#define CAL_START_COMMAND_REG       102U   /* 4WUB0103 */
#define CAL_STOP_COMMAND_REG        103U   /* 4WUB0104 */
#define CAL_FIT_COMMAND_REG         104U   /* 4WUB0105 */
#define CAL_CLEAR_POINTS_REG        105U   /* 4WUB0106 */
#define CAL_CURRENT_POINT_REG       109U   /* 4WUB0110 */
#define CAL_ELAPSED_TIME_REG        110U   /* 4WUB0111 */
#define CAL_STANDARD_VELOCITY_REG   112U   /* 4WUB0113 */
#define CAL_FIT_K_REG               115U   /* 4WUB0116 */
#define CAL_FIT_B_REG               116U   /* 4WUB0117 */
#define CAL_FIT_COEFF_REG_COUNT     2U
#define CAL_STATUS_REG_COUNT        6U
#define CAL_RAW_CURVE_START_REG     119U   /* 4WUB0120 */
#define CAL_STANDARD_CURVE_START_REG 129U  /* 4WUB0130 */
#define GAIN_STATUS_REG             139U    /* 4WUB0140 */
#define GAIN_COMMAND_START_REG      140U    /* 4WUB0141 */
#define GAIN_COMMAND_REG_COUNT      5U
#define PULSE_CONFIG_START_REG      149U    /* 4WUB0150 */
#define PULSE_CONFIG_REG_COUNT      3U
#define MANUAL_CAL_START_REG        159U    /* 4WUB0160 */
#define MANUAL_CAL_REG_COUNT        3U
#define MANUAL_CAL_APPLY_REG        161U    /* 4WUB0162 */
#define CAL_MAX_POINTS              10U
#define MAX_WRITE_REG_COUNT         8U
#define MAX_READ_REG_COUNT          8U
#define VALUE_SCALE                 1000.0f
/* 4WUB0117 displays signed B with X * 0.001 - 10 on the screen. */
#define B_OFFSET_MPS                10.0f
#define TX_TIMEOUT_COUNT            1000000
#define RX_TIMEOUT_COUNT            200000
#define PRE_TX_DELAY_COUNT          200000
#define READ_TURNAROUND_DELAY_COUNT 20000
#define COMMAND_GAP_DELAY_COUNT     200000

static XUartPs uart;
static XGpioPs gpio;

static void delay_loop(volatile int count)
{
    while (count-- > 0) {
    }
}

static uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t position;

    for (position = 0U; position < length; ++position) {
        int bit;
        crc ^= data[position];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1) ^ 0xA001U : crc >> 1;
        }
    }
    return crc;
}

static uint16_t scale_float(float value)
{
    float scaled = value * VALUE_SCALE;

    if (scaled <= 0.0f) {
        return 0U;
    }
    if (scaled >= 65535.0f) {
        return 65535U;
    }
    return (uint16_t)(scaled + 0.5f);
}

static uint16_t scale_b_offset(float value)
{
    return scale_float(value + B_OFFSET_MPS);
}

static float unscale_float(uint16_t value)
{
    return (float)value / VALUE_SCALE;
}

static float unscale_b_offset(uint16_t value)
{
    return unscale_float(value) - B_OFFSET_MPS;
}

static void flush_receive(void)
{
    uint8_t byte;
    while (XUartPs_Recv(&uart, &byte, 1U) > 0U) {
    }
}

static int send_frame(const uint8_t *frame, uint16_t length)
{
    u32 sent = 0U;
    int timeout = TX_TIMEOUT_COUNT;

    XGpioPs_WritePin(&gpio, RS485_DE_MIO, 1U);
    delay_loop(PRE_TX_DELAY_COUNT);

    while (sent < length && timeout > 0) {
        sent += XUartPs_Send(&uart, (uint8_t *)frame + sent, length - sent);
        timeout--;
    }
    while (XUartPs_IsSending(&uart) && timeout > 0) {
        timeout--;
    }
    XGpioPs_WritePin(&gpio, RS485_DE_MIO, 0U);
    return sent == length && timeout > 0 ? XST_SUCCESS : XST_FAILURE;
}

static int receive_exact(uint8_t *data, uint16_t length, int timeout)
{
    u32 received = 0U;

    while (received < length && timeout > 0) {
        int count = XUartPs_Recv(&uart, data + received, length - received);
        if (count > 0) {
            received += (u32)count;
        } else {
            timeout--;
        }
    }
    return received == length ? XST_SUCCESS : XST_FAILURE;
}

static int validate_crc(const uint8_t *frame, uint16_t length)
{
    uint16_t received_crc;

    if (length < 2U) {
        return XST_FAILURE;
    }
    received_crc = (uint16_t)frame[length - 2U] |
                   ((uint16_t)frame[length - 1U] << 8);
    return received_crc == modbus_crc16(frame, length - 2U) ?
           XST_SUCCESS : XST_FAILURE;
}

static int wait_write_echo(uint8_t function,
                           uint16_t address,
                           uint16_t value_or_count)
{
    uint8_t response[8];

    delay_loop(READ_TURNAROUND_DELAY_COUNT);
    if (receive_exact(response, sizeof(response), RX_TIMEOUT_COUNT) != XST_SUCCESS ||
        validate_crc(response, sizeof(response)) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (response[0] != MODBUS_SLAVE_ADDRESS || response[1] != function) {
        return XST_FAILURE;
    }
    if ((((uint16_t)response[2] << 8) | response[3]) != address ||
        (((uint16_t)response[4] << 8) | response[5]) != value_or_count) {
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static int write_single_register(uint16_t address, uint16_t value)
{
    uint8_t frame[8];
    uint16_t crc;

    frame[0] = MODBUS_SLAVE_ADDRESS;
    frame[1] = MODBUS_WRITE_SINGLE_REG;
    frame[2] = (uint8_t)(address >> 8);
    frame[3] = (uint8_t)address;
    frame[4] = (uint8_t)(value >> 8);
    frame[5] = (uint8_t)value;
    crc = modbus_crc16(frame, 6U);
    frame[6] = (uint8_t)crc;
    frame[7] = (uint8_t)(crc >> 8);

    flush_receive();
    if (send_frame(frame, sizeof(frame)) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    return wait_write_echo(MODBUS_WRITE_SINGLE_REG, address, value);
}

static int write_registers(uint16_t start_address,
                           const uint16_t *values,
                           uint16_t count)
{
    uint8_t frame[9U + 2U * MAX_WRITE_REG_COUNT];
    uint16_t index;
    uint16_t crc;
    uint16_t frame_length;

    if (values == NULL || count == 0U || count > MAX_WRITE_REG_COUNT) {
        return XST_FAILURE;
    }

    frame[0] = MODBUS_SLAVE_ADDRESS;
    frame[1] = MODBUS_WRITE_MULTI_REG;
    frame[2] = (uint8_t)(start_address >> 8);
    frame[3] = (uint8_t)start_address;
    frame[4] = (uint8_t)(count >> 8);
    frame[5] = (uint8_t)count;
    frame[6] = (uint8_t)(count * 2U);
    for (index = 0U; index < count; ++index) {
        frame[7U + 2U * index] = (uint8_t)(values[index] >> 8);
        frame[8U + 2U * index] = (uint8_t)values[index];
    }
    frame_length = (uint16_t)(9U + 2U * count);
    crc = modbus_crc16(frame, frame_length - 2U);
    frame[frame_length - 2U] = (uint8_t)crc;
    frame[frame_length - 1U] = (uint8_t)(crc >> 8);

    flush_receive();
    if (send_frame(frame, frame_length) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    return wait_write_echo(MODBUS_WRITE_MULTI_REG, start_address, count);
}

static int read_registers(uint16_t start_address,
                          uint16_t count,
                          uint16_t *values)
{
    uint8_t request[8];
    uint8_t response[3U + 2U * MAX_READ_REG_COUNT + 2U];
    uint16_t crc;
    uint16_t expected_length;
    uint16_t index;

    if (values == NULL || count == 0U || count > MAX_READ_REG_COUNT) {
        return XST_FAILURE;
    }

    request[0] = MODBUS_SLAVE_ADDRESS;
    request[1] = MODBUS_READ_HOLDING_REG;
    request[2] = (uint8_t)(start_address >> 8);
    request[3] = (uint8_t)start_address;
    request[4] = (uint8_t)(count >> 8);
    request[5] = (uint8_t)count;
    crc = modbus_crc16(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8);

    flush_receive();
    if (send_frame(request, sizeof(request)) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    delay_loop(READ_TURNAROUND_DELAY_COUNT);

    if (receive_exact(response, 3U, RX_TIMEOUT_COUNT) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (response[0] != MODBUS_SLAVE_ADDRESS ||
        response[1] == (MODBUS_READ_HOLDING_REG | MODBUS_EXCEPTION_MASK) ||
        response[1] != MODBUS_READ_HOLDING_REG ||
        response[2] != (uint8_t)(count * 2U)) {
        return XST_FAILURE;
    }

    expected_length = (uint16_t)(3U + count * 2U + 2U);
    if (receive_exact(response + 3U,
                      expected_length - 3U,
                      RX_TIMEOUT_COUNT) != XST_SUCCESS ||
        validate_crc(response, expected_length) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    for (index = 0U; index < count; ++index) {
        values[index] = ((uint16_t)response[3U + 2U * index] << 8) |
                        response[4U + 2U * index];
    }
    return XST_SUCCESS;
}

static int write_zero_registers(uint16_t start_address, uint16_t count)
{
    uint16_t zeros[MAX_WRITE_REG_COUNT];
    uint16_t offset = 0U;

    for (uint16_t i = 0U; i < MAX_WRITE_REG_COUNT; ++i) {
        zeros[i] = 0U;
    }

    while (offset < count) {
        uint16_t chunk = count - offset;

        if (chunk > MAX_WRITE_REG_COUNT) {
            chunk = MAX_WRITE_REG_COUNT;
        }
        if (write_registers((uint16_t)(start_address + offset),
                            zeros,
                            chunk) != XST_SUCCESS) {
            return XST_FAILURE;
        }
        offset = (uint16_t)(offset + chunk);
        if (offset < count) {
            delay_loop(COMMAND_GAP_DELAY_COUNT);
        }
    }
    return XST_SUCCESS;
}

int rs485_modbus_init(void)
{
    XUartPs_Config *uart_config;
    XGpioPs_Config *gpio_config;
    XUartPsFormat format;
    int status;

    gpio_config = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    if (gpio_config == NULL) {
        return XST_FAILURE;
    }
    status = XGpioPs_CfgInitialize(&gpio, gpio_config, gpio_config->BaseAddr);
    if (status != XST_SUCCESS) {
        return status;
    }
    XGpioPs_SetDirectionPin(&gpio, RS485_DE_MIO, 1U);
    XGpioPs_SetOutputEnablePin(&gpio, RS485_DE_MIO, 1U);
    XGpioPs_WritePin(&gpio, RS485_DE_MIO, 0U);

    uart_config = XUartPs_LookupConfig(UART_DEVICE_ID);
    if (uart_config == NULL) {
        return XST_FAILURE;
    }
    status = XUartPs_CfgInitialize(&uart,
                                   uart_config,
                                   uart_config->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    format.BaudRate = 9600U;
    format.DataBits = XUARTPS_FORMAT_8_BITS;
    format.Parity = XUARTPS_FORMAT_ODD_PARITY;
    format.StopBits = XUARTPS_FORMAT_1_STOP_BIT;
    status = XUartPs_SetDataFormat(&uart, &format);
    if (status != XST_SUCCESS) {
        return status;
    }
    return XUartPs_SetBaudRate(&uart, 9600U);
}

XGpioPs *rs485_modbus_gpio(void)
{
    return &gpio;
}

int rs485_modbus_write_measurements(float raw_velocity,
                                    float calibrated_velocity,
                                    float flow_rate_m3s,
                                    float total_volume_m3)
{
    uint16_t values[MEASUREMENT_REG_COUNT];

    values[0] = scale_float(raw_velocity);
    values[1] = scale_float(calibrated_velocity);
    values[2] = scale_float(flow_rate_m3s);
    values[3] = scale_float(total_volume_m3);
    return write_registers(MEASUREMENT_START_REG,
                           values,
                           MEASUREMENT_REG_COUNT);
}

int rs485_modbus_poll_reset_request(void)
{
    uint16_t values[RESET_READ_REG_COUNT];

    if (read_registers(RESET_READ_START_REG,
                       RESET_READ_REG_COUNT,
                       values) != XST_SUCCESS) {
        return 0;
    }
    if (values[RESET_BUTTON_INDEX] < RESET_PRESSED_VALUE) {
        return 0;
    }

    /* Match the display reset sequence: leave a Modbus command gap before
     * the caller writes the cleared measurement values. */
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return 1;
}

int rs485_modbus_clear_reset_request(void)
{
    /* Clear the display button only after the zero measurement frame has
     * completed, with the same inter-command gap used by the reference. */
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(RESET_BUTTON_REG, 0U);
}

int rs485_modbus_read_calibration_controls(Rs485CalibrationControls *controls)
{
    uint16_t values[CAL_CONTROL_REG_COUNT];

    if (controls == NULL) {
        return XST_FAILURE;
    }
    if (read_registers(CAL_CONTROL_START_REG,
                       CAL_CONTROL_REG_COUNT,
                       values) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    controls->target_points = values[0];
    controls->timing_mode = values[1];
    controls->sample_time_s = values[2];
    controls->start_command = values[3];
    controls->stop_command = values[4];
    controls->fit_command = values[5];
    controls->clear_points_command = values[6];
    return XST_SUCCESS;
}

int rs485_modbus_clear_calibration_start(void)
{
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(CAL_START_COMMAND_REG, 0U);
}

int rs485_modbus_clear_calibration_stop(void)
{
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(CAL_STOP_COMMAND_REG, 0U);
}

int rs485_modbus_clear_calibration_fit(void)
{
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(CAL_FIT_COMMAND_REG, 0U);
}

int rs485_modbus_clear_calibration_points_command(void)
{
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(CAL_CLEAR_POINTS_REG, 0U);
}

int rs485_modbus_clear_calibration_setup(void)
{
    uint16_t control_zeros[CAL_CONTROL_REG_COUNT];
    uint16_t status_zeros[2] = {0U, 0U};
    int status;

    for (uint16_t i = 0U; i < CAL_CONTROL_REG_COUNT; ++i) {
        control_zeros[i] = 0U;
    }

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    status = write_registers(CAL_CONTROL_START_REG,
                             control_zeros,
                             CAL_CONTROL_REG_COUNT);
    if (status != XST_SUCCESS) {
        return status;
    }

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_registers(CAL_CURRENT_POINT_REG,
                           status_zeros,
                           2U);
}

int rs485_modbus_clear_calibration_curves(void)
{
    int status;

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    status = write_zero_registers(CAL_RAW_CURVE_START_REG, CAL_MAX_POINTS);
    if (status != XST_SUCCESS) {
        return status;
    }

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_zero_registers(CAL_STANDARD_CURVE_START_REG, CAL_MAX_POINTS);
}

int rs485_modbus_read_gain_controls(Rs485GainControls *controls)
{
    uint16_t values[GAIN_COMMAND_REG_COUNT];

    if (controls == NULL) {
        return XST_FAILURE;
    }
    if (read_registers(GAIN_COMMAND_START_REG,
                       GAIN_COMMAND_REG_COUNT,
                       values) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    controls->gain_1_command = values[0];
    controls->gain_2_command = values[1];
    controls->gain_3_command = values[2];
    controls->gain_4_command = values[3];
    controls->adaptive_gain_command = values[4];
    return XST_SUCCESS;
}

int rs485_modbus_clear_gain_commands(void)
{
    uint16_t zeros[GAIN_COMMAND_REG_COUNT];

    for (uint16_t i = 0U; i < GAIN_COMMAND_REG_COUNT; ++i) {
        zeros[i] = 0U;
    }

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_registers(GAIN_COMMAND_START_REG,
                           zeros,
                           GAIN_COMMAND_REG_COUNT);
}

int rs485_modbus_write_gain_status(u16 gain_stage)
{
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(GAIN_STATUS_REG, gain_stage);
}

int rs485_modbus_read_calibration_coefficients(float *fit_k, float *fit_b)
{
    uint16_t values[CAL_FIT_COEFF_REG_COUNT];

    if (fit_k == NULL || fit_b == NULL) {
        return XST_FAILURE;
    }
    if (read_registers(CAL_FIT_K_REG,
                       CAL_FIT_COEFF_REG_COUNT,
                       values) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (values[0] == 0U || values[1] == 0U) {
        return XST_FAILURE;
    }

    *fit_k = unscale_float(values[0]);
    *fit_b = unscale_b_offset(values[1]);
    return XST_SUCCESS;
}

int rs485_modbus_read_manual_calibration_controls(
    Rs485ManualCalibrationControls *controls)
{
    uint16_t values[MANUAL_CAL_REG_COUNT];

    if (controls == NULL) {
        return XST_FAILURE;
    }
    if (read_registers(MANUAL_CAL_START_REG,
                       MANUAL_CAL_REG_COUNT,
                       values) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    controls->fit_k_raw = values[0];
    controls->fit_b_raw = values[1];
    controls->apply_command = values[2];
    return XST_SUCCESS;
}

int rs485_modbus_clear_manual_calibration_apply(void)
{
    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(MANUAL_CAL_APPLY_REG, 0U);
}

int rs485_modbus_read_pulse_config(Rs485PulseConfig *config)
{
    uint16_t values[PULSE_CONFIG_REG_COUNT];

    if (config == NULL) {
        return XST_FAILURE;
    }
    if (read_registers(PULSE_CONFIG_START_REG,
                       PULSE_CONFIG_REG_COUNT,
                       values) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    config->pulse_volume_milliliter = values[0];
    config->pulse_count = values[1];
    config->pulse_width_ms = values[2];
    return XST_SUCCESS;
}

int rs485_modbus_write_calibration_status(u16 point_count,
                                          u16 elapsed_time_s,
                                          float standard_velocity,
                                          float raw_average,
                                          float standard_average,
                                          float fit_k,
                                          float fit_b,
                                          u16 calibration_status)
{
    uint16_t count_values[2];
    uint16_t status_values[CAL_STATUS_REG_COUNT];
    int status;

    count_values[0] = point_count;
    count_values[1] = elapsed_time_s;
    status = write_registers(CAL_CURRENT_POINT_REG,
                             count_values,
                             2U);
    if (status != XST_SUCCESS) {
        return status;
    }

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    status_values[0] = scale_float(standard_velocity);
    status_values[1] = scale_float(raw_average);
    status_values[2] = scale_float(standard_average);
    status_values[3] = scale_float(fit_k);
    status_values[4] = scale_b_offset(fit_b);
    status_values[5] = calibration_status;
    return write_registers(CAL_STANDARD_VELOCITY_REG,
                           status_values,
                           CAL_STATUS_REG_COUNT);
}

int rs485_modbus_write_calibration_point(u16 point_index,
                                         float raw_average,
                                         float standard_average)
{
    int status;

    if (point_index == 0U || point_index > CAL_MAX_POINTS) {
        return XST_FAILURE;
    }

    status = write_single_register(
        (uint16_t)(CAL_RAW_CURVE_START_REG + point_index - 1U),
        scale_float(raw_average));
    if (status != XST_SUCCESS) {
        return status;
    }

    delay_loop(COMMAND_GAP_DELAY_COUNT);
    return write_single_register(
        (uint16_t)(CAL_STANDARD_CURVE_START_REG + point_index - 1U),
        scale_float(standard_average));
}
