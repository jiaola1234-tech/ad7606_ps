#include <math.h>
#include <stdint.h>
#include <string.h>

#include "ad7606_dma.h"
#include "app_config.h"

#include "can.h"
#include "external_flow.h"
#include "flow_pulse.h"
#include "interrupt_controller.h"
#include "platform.h"
#include "rs485_modbus.h"
#include "tcp_stream.h"
#include "xparameters.h"
#include "xqspips.h"
#include "xscugic.h"
#include "xstatus.h"
#include "xtime_l.h"

#define CAL_MAX_POINTS       10U
#define CAL_TIMING_TIMED     0U
#define CAL_TIMING_MANUAL    1U
#define CAL_STATUS_IDLE      0U
#define CAL_STATUS_COLLECTING 1U
#define CAL_STATUS_GROUP_DONE 2U
#define CAL_STATUS_FIT_DONE  3U
#define CAL_STATUS_ERR_NO_TARGET 101U
#define CAL_STATUS_ERR_FULL  102U
#define CAL_STATUS_ERR_FIT_POINTS 103U
#define CAL_STATUS_ERR_FIT_VARIATION 104U
#define CAL_FIT_OK           0U
#define CAL_FIT_NOT_ENOUGH   1U
#define CAL_FIT_NO_VARIATION 2U
#define CAL_CONTROL_POLL_MS  200ULL
#define PULSE_DEFAULT_VOLUME_MILLILITER 1000U
#define PULSE_DEFAULT_COUNT 1U
#define PULSE_DEFAULT_WIDTH_US 20000U
#define CAL_FLASH_OFFSET     0x00FF0000U
#define CAL_FLASH_MAGIC      0x4B424346U
#define CAL_FLASH_VERSION    1U
#define CAL_FLASH_ERASE_MS   5000ULL
#define CAL_FLASH_WRITE_MS   1000ULL

typedef struct {
    float real;
    float imag;
} complex_t;

typedef struct {
    float raw_points[CAL_MAX_POINTS];
    float standard_points[CAL_MAX_POINTS];
    double raw_sum;
    double standard_sum;
    uint64_t sample_count;
    uint64_t start_time_ms;
    float raw_total_start_liter;
    float standard_total_start_liter;
    uint16_t point_count;
    uint16_t target_points;
    uint16_t timing_mode;
    uint16_t sample_time_s;
    uint16_t elapsed_time_s;
    uint16_t last_reported_elapsed_s;
    float raw_average;
    float standard_average;
    float standard_velocity;
    float fit_k;
    float fit_b;
    uint16_t status_code;
    int collecting;
} CalibrationState;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    float fit_k;
    float fit_b;
    uint32_t checksum;
} CalibrationFlashRecord;

static XScuGic interrupt_controller;
static XQspiPs calibration_flash_qspi;
static int calibration_flash_ready;

static float adc_buffer[RAW_WINDOW_SAMPLES];
static float window[FFT_SIZE];
static complex_t buf[FFT_SIZE];
static complex_t fft_out[NUM_FFT_BLOCKS][NUM_CHANNELS][FFT_SIZE];
static complex_t R_all[NUM_FREQ][NUM_CHANNELS][NUM_CHANNELS];
static complex_t R_history[NUM_FREQ][NUM_CHANNELS][NUM_CHANNELS];
static float P_music[NUM_FREQ][NUM_K];
static float P_db[NUM_FREQ][NUM_K];
static int covariance_history_valid;
static float velocity_history[3];
static int velocity_history_count;
static int velocity_history_index;

static float u_opt;
static volatile float u_opt_raw;
static volatile float u_peak_raw;
static volatile float u_peak_avg_start;
static volatile float u_peak_avg_end;
static volatile float u_peak_avg_area;
static volatile float u_peak_avg_weighted_area;
static volatile float flow_rate_m3s;
static uint16_t gain_stage = 4U;
static uint16_t pulse_volume_milliliter = PULSE_DEFAULT_VOLUME_MILLILITER;
static uint16_t pulse_count = PULSE_DEFAULT_COUNT;
static uint16_t pulse_width_us = PULSE_DEFAULT_WIDTH_US;
static volatile int steering_vector_sign = 1;
static CalibrationState calibration_state;

static float f_axis[NUM_FREQ];
static float k_axis[NUM_K];
static float u_trials[NUM_U];
static float energy[NUM_U];
static float u_range[2] = {0.5f, 8.0f};

static const float proc_positions[NUM_CHANNELS] = {
    0.0f,
    1.0f * 0.0605f,
    2.0f * 0.0605f,
    3.0f * 0.0605f,
    4.0f * 0.0605f,
    5.0f * 0.0605f,
    6.0f * 0.0605f,
    7.0f * 0.0605f
};

float calibrate_velocity(float velocity_mps);
float velocity_to_flow_rate(float velocity_mps);
static void calibration_flash_restore_coefficients(void);
static int calibration_flash_save_coefficients(float fit_k, float fit_b);
static void steering_vector_apply(uint16_t selection);

static uint64_t get_time_ms(void)
{
    XTime now;
    uint64_t counts;

    XTime_GetTime(&now);
    counts = (uint64_t)now;
    return counts * 1000ULL / (uint64_t)COUNTS_PER_SECOND;
}

static uint32_t calibration_flash_checksum(const CalibrationFlashRecord *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t checksum = 0xA5A55A5AU;
    uint32_t count = sizeof(*record) - sizeof(record->checksum);

    for (uint32_t i = 0U; i < count; ++i) {
        checksum = (checksum << 5) ^ (checksum >> 27) ^ bytes[i];
    }
    return checksum;
}

static int calibration_coefficients_are_valid(float fit_k, float fit_b)
{
    if (fit_k <= 0.0f || fit_k > 100.0f) {
        return 0;
    }
    if (fit_b < -100.0f || fit_b > 100.0f) {
        return 0;
    }
    return 1;
}

static int calibration_flash_record_is_valid(
    const CalibrationFlashRecord *record)
{
    if (record == NULL) {
        return 0;
    }
    if (record->magic != CAL_FLASH_MAGIC ||
        record->version != CAL_FLASH_VERSION ||
        record->size != sizeof(*record)) {
        return 0;
    }
    if (record->checksum != calibration_flash_checksum(record)) {
        return 0;
    }
    return calibration_coefficients_are_valid(record->fit_k, record->fit_b);
}

static int calibration_flash_init(void)
{
    XQspiPs_Config *config;
    int status;

    if (calibration_flash_ready) {
        return XST_SUCCESS;
    }

    config = XQspiPs_LookupConfig(XPAR_XQSPIPS_0_DEVICE_ID);
    if (config == NULL) {
        return XST_FAILURE;
    }

    status = XQspiPs_CfgInitialize(&calibration_flash_qspi,
                                   config,
                                   config->BaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XQspiPs_SetOptions(&calibration_flash_qspi,
                                XQSPIPS_FORCE_SSELECT_OPTION |
                                XQSPIPS_HOLD_B_DRIVE_OPTION);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XQspiPs_SetClkPrescaler(&calibration_flash_qspi,
                                     XQSPIPS_CLK_PRESCALE_8);
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XQspiPs_SetSlaveSelect(&calibration_flash_qspi);
    if (status != XST_SUCCESS) {
        return status;
    }

    calibration_flash_ready = 1;
    return XST_SUCCESS;
}

static void calibration_flash_set_address(uint8_t *command, uint32_t address)
{
    command[1] = (uint8_t)((address >> 16) & 0xFFU);
    command[2] = (uint8_t)((address >> 8) & 0xFFU);
    command[3] = (uint8_t)(address & 0xFFU);
}

static int calibration_flash_write_enable(void)
{
    uint8_t command = 0x06U;

    return XQspiPs_PolledTransfer(&calibration_flash_qspi,
                                  &command,
                                  NULL,
                                  1U);
}

static int calibration_flash_read_status(uint8_t *status_value)
{
    uint8_t tx[2] = {0x05U, 0x00U};
    uint8_t rx[2] = {0U, 0U};
    int status;

    if (status_value == NULL) {
        return XST_FAILURE;
    }

    status = XQspiPs_PolledTransfer(&calibration_flash_qspi,
                                    tx,
                                    rx,
                                    sizeof(tx));
    if (status != XST_SUCCESS) {
        return status;
    }

    *status_value = rx[1];
    return XST_SUCCESS;
}

static int calibration_flash_wait_ready(uint64_t timeout_ms)
{
    uint64_t start_ms = get_time_ms();
    uint8_t status_value;

    do {
        if (calibration_flash_read_status(&status_value) != XST_SUCCESS) {
            return XST_FAILURE;
        }
        if ((status_value & 0x01U) == 0U) {
            return XST_SUCCESS;
        }
    } while ((get_time_ms() - start_ms) < timeout_ms);

    return XST_FAILURE;
}

static int calibration_flash_read_record(CalibrationFlashRecord *record)
{
    uint8_t tx[4U + sizeof(*record)];
    uint8_t rx[4U + sizeof(*record)];
    int status;

    if (record == NULL) {
        return XST_FAILURE;
    }
    if (calibration_flash_init() != XST_SUCCESS) {
        return XST_FAILURE;
    }

    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    tx[0] = 0x03U;
    calibration_flash_set_address(tx, CAL_FLASH_OFFSET);

    status = XQspiPs_PolledTransfer(&calibration_flash_qspi,
                                    tx,
                                    rx,
                                    sizeof(tx));
    if (status != XST_SUCCESS) {
        return status;
    }

    memcpy(record, &rx[4], sizeof(*record));
    return XST_SUCCESS;
}

static int calibration_flash_erase_sector(void)
{
    uint8_t command[4] = {0xD8U, 0U, 0U, 0U};
    int status;

    status = calibration_flash_write_enable();
    if (status != XST_SUCCESS) {
        return status;
    }

    calibration_flash_set_address(command, CAL_FLASH_OFFSET);
    status = XQspiPs_PolledTransfer(&calibration_flash_qspi,
                                    command,
                                    NULL,
                                    sizeof(command));
    if (status != XST_SUCCESS) {
        return status;
    }

    return calibration_flash_wait_ready(CAL_FLASH_ERASE_MS);
}

static int calibration_flash_program_record(
    const CalibrationFlashRecord *record)
{
    uint8_t tx[4U + sizeof(*record)];
    int status;

    memset(tx, 0, sizeof(tx));
    tx[0] = 0x02U;
    calibration_flash_set_address(tx, CAL_FLASH_OFFSET);
    memcpy(&tx[4], record, sizeof(*record));

    status = calibration_flash_write_enable();
    if (status != XST_SUCCESS) {
        return status;
    }

    status = XQspiPs_PolledTransfer(&calibration_flash_qspi,
                                    tx,
                                    NULL,
                                    sizeof(tx));
    if (status != XST_SUCCESS) {
        return status;
    }

    return calibration_flash_wait_ready(CAL_FLASH_WRITE_MS);
}

static int calibration_flash_save_coefficients(float fit_k, float fit_b)
{
    CalibrationFlashRecord record;
    CalibrationFlashRecord verify_record;

    if (!calibration_coefficients_are_valid(fit_k, fit_b)) {
        return XST_FAILURE;
    }
    if (calibration_flash_init() != XST_SUCCESS) {
        return XST_FAILURE;
    }

    memset(&record, 0, sizeof(record));
    record.magic = CAL_FLASH_MAGIC;
    record.version = CAL_FLASH_VERSION;
    record.size = sizeof(record);
    record.fit_k = fit_k;
    record.fit_b = fit_b;
    record.checksum = calibration_flash_checksum(&record);

    if (calibration_flash_erase_sector() != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (calibration_flash_program_record(&record) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (calibration_flash_read_record(&verify_record) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    if (memcmp(&record, &verify_record, sizeof(record)) != 0) {
        return XST_FAILURE;
    }

    return XST_SUCCESS;
}

static void calibration_flash_restore_coefficients(void)
{
    CalibrationFlashRecord record;

    if (calibration_flash_read_record(&record) != XST_SUCCESS) {
        return;
    }
    if (!calibration_flash_record_is_valid(&record)) {
        return;
    }

    calibration_state.fit_k = record.fit_k;
    calibration_state.fit_b = record.fit_b;
}

static uint16_t calibration_clamp_target_points(uint16_t target_points)
{
    if (target_points > CAL_MAX_POINTS) {
        return CAL_MAX_POINTS;
    }
    return target_points;
}

static void calibration_clear_points(void)
{
    for (uint16_t i = 0U; i < CAL_MAX_POINTS; ++i) {
        calibration_state.raw_points[i] = 0.0f;
        calibration_state.standard_points[i] = 0.0f;
    }
    calibration_state.raw_sum = 0.0;
    calibration_state.standard_sum = 0.0;
    calibration_state.sample_count = 0U;
    calibration_state.start_time_ms = 0U;
    calibration_state.raw_total_start_liter = 0.0f;
    calibration_state.standard_total_start_liter = 0.0f;
    calibration_state.point_count = 0U;
    calibration_state.target_points = 0U;
    calibration_state.elapsed_time_s = 0U;
    calibration_state.last_reported_elapsed_s = 0U;
    calibration_state.standard_velocity = 0.0f;
    calibration_state.raw_average = 0.0f;
    calibration_state.standard_average = 0.0f;
    calibration_state.collecting = 0;
}

static float standard_velocity_from_flow_l_min(float flow_l_min)
{
    float radius_m = PIPE_DIAMETER_M * 0.5f;
    float area_m2 = PI * radius_m * radius_m;
    float flow_m3s = flow_l_min / 1000.0f / 60.0f;

    if (area_m2 <= EPS || flow_l_min <= 0.0f) {
        return 0.0f;
    }
    return flow_m3s / area_m2;
}

static float velocity_from_total_liter_and_elapsed(float total_liter,
                                                   uint64_t elapsed_ms)
{
    float elapsed_s;
    float average_flow_l_min;

    if (total_liter <= 0.0f || elapsed_ms == 0U) {
        return 0.0f;
    }

    elapsed_s = (float)((double)elapsed_ms / 1000.0);
    if (elapsed_s <= 0.0f) {
        return 0.0f;
    }

    average_flow_l_min = total_liter * 60.0f / elapsed_s;
    return standard_velocity_from_flow_l_min(average_flow_l_min);
}

static void calibration_update_collection_metrics(void)
{
    ExternalFlowStats stats;
    uint64_t elapsed_ms;
    float raw_delta_liter;
    float standard_delta_liter;

    if (!calibration_state.collecting ||
        calibration_state.start_time_ms == 0U) {
        return;
    }

    elapsed_ms = get_time_ms() - calibration_state.start_time_ms;
    external_flow_get_stats(&stats);
    raw_delta_liter = stats.raw_total_liter -
                      calibration_state.raw_total_start_liter;
    standard_delta_liter = stats.standard_total_liter -
                           calibration_state.standard_total_start_liter;
    if (raw_delta_liter < 0.0f) {
        raw_delta_liter = 0.0f;
    }
    if (standard_delta_liter < 0.0f) {
        standard_delta_liter = 0.0f;
    }

    calibration_state.standard_velocity =
        standard_velocity_from_flow_l_min(stats.standard_flow_l_min);
    calibration_state.raw_average =
        velocity_from_total_liter_and_elapsed(raw_delta_liter,
                                              elapsed_ms);
    calibration_state.standard_average =
        velocity_from_total_liter_and_elapsed(standard_delta_liter,
                                              elapsed_ms);
}

static void calibration_init(void)
{
    calibration_state.timing_mode = CAL_TIMING_TIMED;
    calibration_state.sample_time_s = 0U;
    calibration_state.standard_velocity = 0.0f;
    calibration_state.fit_k = U_CAL_B;
    calibration_state.fit_b = U_CAL_C;
    calibration_clear_points();
    calibration_state.status_code = CAL_STATUS_IDLE;
}

static void calibration_update_elapsed(void)
{
    uint64_t elapsed_ms;

    if (!calibration_state.collecting ||
        calibration_state.start_time_ms == 0U) {
        return;
    }

    elapsed_ms = get_time_ms() - calibration_state.start_time_ms;
    if (elapsed_ms / 1000ULL > 65535ULL) {
        calibration_state.elapsed_time_s = 65535U;
    } else {
        calibration_state.elapsed_time_s = (uint16_t)(elapsed_ms / 1000ULL);
    }
}

static void calibration_start_group(const Rs485CalibrationControls *controls)
{
    ExternalFlowStats stats;
    uint16_t target_points;

    if (controls == NULL) {
        return;
    }

    if (calibration_state.collecting) {
        calibration_state.status_code = CAL_STATUS_COLLECTING;
        return;
    }

    target_points = calibration_clamp_target_points(controls->target_points);
    calibration_state.target_points = target_points;
    calibration_state.timing_mode =
        controls->timing_mode == CAL_TIMING_MANUAL ?
        CAL_TIMING_MANUAL : CAL_TIMING_TIMED;
    calibration_state.sample_time_s = controls->sample_time_s;

    if (target_points == 0U ||
        calibration_state.point_count >= target_points ||
        calibration_state.point_count >= CAL_MAX_POINTS) {
        calibration_state.status_code =
            target_points == 0U ? CAL_STATUS_ERR_NO_TARGET :
            CAL_STATUS_ERR_FULL;
        return;
    }

    calibration_state.raw_sum = 0.0;
    calibration_state.standard_sum = 0.0;
    calibration_state.sample_count = 0U;
    external_flow_get_stats(&stats);
    calibration_state.raw_total_start_liter = stats.raw_total_liter;
    calibration_state.standard_total_start_liter = stats.standard_total_liter;
    calibration_state.start_time_ms = get_time_ms();
    calibration_state.elapsed_time_s = 0U;
    calibration_state.last_reported_elapsed_s = 0U;
    calibration_state.standard_velocity = 0.0f;
    calibration_state.raw_average = 0.0f;
    calibration_state.standard_average = 0.0f;
    calibration_state.collecting = 1;
    calibration_state.status_code = CAL_STATUS_COLLECTING;
}

static uint16_t calibration_finish_group(void)
{
    uint16_t point_index;

    if (!calibration_state.collecting) {
        return 0U;
    }

    calibration_update_elapsed();
    calibration_update_collection_metrics();
    calibration_state.collecting = 0;
    if (calibration_state.sample_count == 0U ||
        calibration_state.point_count >= CAL_MAX_POINTS) {
        calibration_state.start_time_ms = 0U;
        calibration_state.elapsed_time_s = 0U;
        return 0U;
    }

    point_index = calibration_state.point_count;
    calibration_state.raw_points[point_index] =
        calibration_state.raw_average;
    calibration_state.standard_points[point_index] =
        calibration_state.standard_average;
    calibration_state.point_count++;
    calibration_state.start_time_ms = 0U;
    calibration_state.elapsed_time_s = 0U;
    calibration_state.last_reported_elapsed_s = 0U;
    calibration_state.status_code = CAL_STATUS_GROUP_DONE;
    return calibration_state.point_count;
}

static uint16_t calibration_add_sample(float raw_velocity,
                                       float standard_velocity)
{
    if (!calibration_state.collecting) {
        return 0U;
    }

    calibration_state.raw_sum += raw_velocity;
    calibration_state.standard_sum += standard_velocity;
    calibration_state.sample_count++;
    calibration_update_elapsed();
    calibration_update_collection_metrics();

    if (calibration_state.timing_mode == CAL_TIMING_TIMED &&
        calibration_state.sample_time_s > 0U &&
        calibration_state.elapsed_time_s >= calibration_state.sample_time_s) {
        return calibration_finish_group();
    }
    return 0U;
}

static uint16_t calibration_fit_points(void)
{
    double mean_x = 0.0;
    double mean_y = 0.0;
    double numerator = 0.0;
    double denominator = 0.0;
    uint16_t count = calibration_state.point_count;

    if (count < 2U) {
        return CAL_FIT_NOT_ENOUGH;
    }

    for (uint16_t i = 0U; i < count; ++i) {
        mean_x += calibration_state.raw_points[i];
        mean_y += calibration_state.standard_points[i];
    }
    mean_x /= (double)count;
    mean_y /= (double)count;

    for (uint16_t i = 0U; i < count; ++i) {
        double dx = (double)calibration_state.raw_points[i] - mean_x;
        double dy = (double)calibration_state.standard_points[i] - mean_y;
        numerator += dx * dy;
        denominator += dx * dx;
    }

    if (fabs(denominator) <= (double)EPS) {
        return CAL_FIT_NO_VARIATION;
    }

    calibration_state.fit_k = (float)(numerator / denominator);
    calibration_state.fit_b =
        (float)(mean_y - (double)calibration_state.fit_k * mean_x);
    return CAL_FIT_OK;
}

static uint16_t calibration_display_current_point(void)
{
    if (calibration_state.target_points == 0U) {
        return 0U;
    }
    if (calibration_state.point_count >= calibration_state.target_points) {
        return calibration_state.target_points;
    }
    return (uint16_t)(calibration_state.point_count + 1U);
}

static void calibration_write_status(void)
{
    (void)rs485_modbus_write_calibration_status(
        calibration_display_current_point(),
        calibration_state.elapsed_time_s,
        calibration_state.standard_velocity,
        calibration_state.raw_average,
        calibration_state.standard_average,
        calibration_state.fit_k,
        calibration_state.fit_b,
        calibration_state.status_code);
}


static void calibration_fit_and_clear_setup(void)
{
    uint16_t fit_status;

    fit_status = calibration_fit_points();
    if (fit_status == CAL_FIT_OK) {
        calibration_state.status_code = CAL_STATUS_FIT_DONE;
        external_flow_set_calibration_factor(calibration_state.fit_k);
        (void)calibration_flash_save_coefficients(
            calibration_state.fit_k,
            calibration_state.fit_b);
    } else if (fit_status == CAL_FIT_NOT_ENOUGH) {
        calibration_state.status_code = CAL_STATUS_ERR_FIT_POINTS;
    } else {
        calibration_state.status_code = CAL_STATUS_ERR_FIT_VARIATION;
    }

    (void)rs485_modbus_clear_calibration_setup();
    calibration_write_status();
}

static int calibration_poll_manual_apply(void)
{
    Rs485ManualCalibrationControls controls;
    float fit_k;
    float fit_b;

    if (rs485_modbus_read_manual_calibration_controls(&controls) !=
        XST_SUCCESS) {
        return 0;
    }

    steering_vector_apply(controls.steering_vector_command);
    if (calibration_state.collecting) {
        return 0;
    }
    if (controls.apply_command == 0U) {
        return 0;
    }

    (void)rs485_modbus_clear_manual_calibration_apply();
    if (controls.fit_k_raw == 0U) {
        return 0;
    }

    fit_k = (float)controls.fit_k_raw / 1000.0f;
    fit_b = (float)controls.fit_b_raw / 1000.0f;
    if (fabsf(fit_k - calibration_state.fit_k) <= 0.0005f &&
        fabsf(fit_b - calibration_state.fit_b) <= 0.0005f) {
        (void)calibration_flash_save_coefficients(fit_k, fit_b);
        calibration_write_status();
        return 0;
    }

    calibration_state.fit_k = fit_k;
    calibration_state.fit_b = fit_b;
    calibration_state.status_code = CAL_STATUS_IDLE;
    external_flow_set_calibration_factor(calibration_state.fit_k);
    (void)calibration_flash_save_coefficients(calibration_state.fit_k,
                                              calibration_state.fit_b);
    calibration_write_status();
    return 1;
}

static void calibration_refresh_collecting_status(void)
{
    if (!calibration_state.collecting) {
        return;
    }

    calibration_update_elapsed();
    calibration_update_collection_metrics();
    if (calibration_state.elapsed_time_s ==
        calibration_state.last_reported_elapsed_s) {
        return;
    }

    calibration_state.last_reported_elapsed_s =
        calibration_state.elapsed_time_s;
    calibration_write_status();
}

static void calibration_write_saved_point(uint16_t point_index)
{
    if (point_index == 0U || point_index > CAL_MAX_POINTS) {
        return;
    }

    (void)rs485_modbus_write_calibration_point(
        point_index,
        calibration_state.raw_points[point_index - 1U],
        calibration_state.standard_points[point_index - 1U]);
}





static int calibration_handle_target_change(
    const Rs485CalibrationControls *controls)
{
    uint16_t target_points;

    if (controls == NULL || calibration_state.collecting) {
        return 0;
    }

    target_points = calibration_clamp_target_points(controls->target_points);
    if (target_points == calibration_state.target_points) {
        return 0;
    }

    calibration_clear_points();
    calibration_state.target_points = target_points;
    calibration_state.timing_mode =
        controls->timing_mode == CAL_TIMING_MANUAL ?
        CAL_TIMING_MANUAL : CAL_TIMING_TIMED;
    calibration_state.sample_time_s = controls->sample_time_s;
    calibration_state.status_code = CAL_STATUS_IDLE;
    calibration_state.last_reported_elapsed_s = 0U;
    (void)rs485_modbus_clear_calibration_curves();
    calibration_write_status();
    return 1;
}

static void calibration_clear_points_command(void)
{
    calibration_clear_points();
    calibration_state.status_code = CAL_STATUS_IDLE;
    calibration_state.last_reported_elapsed_s = 0U;
    (void)rs485_modbus_clear_calibration_curves();
    (void)rs485_modbus_clear_calibration_points_command();
    calibration_write_status();
}

static void calibration_poll_controls(void)
{
    Rs485CalibrationControls controls;
    uint16_t saved_point;
    int status;

    status = rs485_modbus_read_calibration_controls(&controls);
    if (status != XST_SUCCESS) {
        return;
    }

    if (controls.clear_points_command >= 1U) {
        calibration_clear_points_command();
        return;
    }

    (void)calibration_handle_target_change(&controls);

    if (controls.start_command >= 1U) {
        (void)rs485_modbus_clear_calibration_start();
        calibration_start_group(&controls);
        calibration_write_status();
    }
    if (controls.stop_command >= 1U) {
        (void)rs485_modbus_clear_calibration_stop();
        saved_point = calibration_finish_group();
        if (saved_point != 0U) {
            calibration_write_saved_point(saved_point);
        }
        if (calibration_state.target_points > 0U &&
            calibration_state.point_count >= calibration_state.target_points) {
            calibration_fit_and_clear_setup();
        } else {
            calibration_write_status();
        }
    }
    if (controls.fit_command >= 1U) {
        (void)rs485_modbus_clear_calibration_fit();
        saved_point = 0U;
        if (calibration_state.collecting) {
            saved_point = calibration_finish_group();
        }
        if (saved_point != 0U) {
            calibration_write_saved_point(saved_point);
        }
        calibration_fit_and_clear_setup();
    }
}

static void gain_apply_stage(uint16_t stage)
{
    int status = can_send_gain_level(stage);

    (void)rs485_modbus_clear_gain_commands();

    /* Do not report a new stage to the display if no gain command was sent. */
    if (status != XST_SUCCESS) {
        return;
    }

    gain_stage = stage;
    (void)rs485_modbus_write_gain_status(gain_stage);
}

static void pulse_config_apply(uint16_t volume_milliliter,
                               uint16_t count,
                               uint16_t width_us)
{
    pulse_volume_milliliter = volume_milliliter;
    pulse_count = count;
    pulse_width_us = width_us;
    flow_pulse_set_config((float)volume_milliliter / 1000.0f,
                          count,
                          width_us);
}

static void steering_vector_apply(uint16_t selection)
{
    /* 0 means reverse (-1); any other value means forward (+1). */
    steering_vector_sign = (selection == 0U) ? -1 : 1;
}

static void pulse_config_sync(void)
{
    Rs485PulseConfig config;

    if (rs485_modbus_read_pulse_config(&config) != XST_SUCCESS) {
        return;
    }
    if (config.pulse_volume_milliliter == 0U ||
        config.pulse_count == 0U ||
        config.pulse_width_us == 0U) {
        return;
    }
    if (config.pulse_volume_milliliter == pulse_volume_milliliter &&
        config.pulse_count == pulse_count &&
        config.pulse_width_us == pulse_width_us) {
        return;
    }

    pulse_config_apply(config.pulse_volume_milliliter,
                       config.pulse_count,
                       config.pulse_width_us);
}

static int gain_init(void)
{
    int status;

    status = can_init(rs485_modbus_gpio());
    if (status != XST_SUCCESS) {
        return status;
    }

    gain_stage = 4U;
    (void)rs485_modbus_clear_gain_commands();
    status = can_send_gain_level(gain_stage);
    if (status != XST_SUCCESS) {
        return status;
    }
    (void)rs485_modbus_write_gain_status(gain_stage);
    return XST_SUCCESS;
}

static void gain_poll_controls(void)
{
    Rs485GainControls controls;
    int status;

    status = rs485_modbus_read_gain_controls(&controls);
    if (status != XST_SUCCESS) {
        return;
    }

    if (controls.gain_1_command >= 1U) {
        gain_apply_stage(1U);
        return;
    }
    if (controls.gain_2_command >= 1U) {
        gain_apply_stage(2U);
        return;
    }
    if (controls.gain_3_command >= 1U) {
        gain_apply_stage(3U);
        return;
    }
    if (controls.gain_4_command >= 1U) {
        gain_apply_stage(4U);
        return;
    }
    if (controls.adaptive_gain_command >= 1U) {
        gain_apply_stage(5U);
        return;
    }
}

void linspace(float start, float end, int N, float *arr)
{
    if (N == 1) {
        arr[0] = start;
        return;
    }
    float step = (end - start) / (N - 1);
    for (int i = 0; i < N; i++) {
        arr[i] = start + i * step;
    }
}

float interp1(const float *x, const float *y, int len, float xi)
{
    if (xi <= x[0]) return y[0];
    if (xi >= x[len - 1]) return y[len - 1];

    for (int i = 0; i < len - 1; i++) {
        if (xi >= x[i] && xi <= x[i + 1]) {
            float t = (xi - x[i]) / (x[i + 1] - x[i]);
            return y[i] * (1.0f - t) + y[i + 1] * t;
        }
    }
    return 0.0f;
}

void movmean(const float *src, int len, int window_len, float *dst)
{
    int half = window_len / 2;
    for (int i = 0; i < len; i++) {
        float sum = 0.0f;
        int cnt = 0;
        for (int j = i - half; j <= i + half; j++) {
            if (j >= 0 && j < len) {
                sum += src[j];
                cnt++;
            }
        }
        dst[i] = (cnt > 0) ? (sum / cnt) : 0.0f;
    }
}

int find_peak_index(const float *data, int len)
{
    int loc = 0;
    float max_val = data[0];

    for (int i = 1; i < len - 1; i++) {
        if (data[i] > data[i - 1] && data[i] > data[i + 1] && data[i] > max_val) {
            max_val = data[i];
            loc = i;
        }
    }

    if (max_val == data[0]) {
        for (int i = 1; i < len; i++) {
            if (data[i] > max_val) {
                max_val = data[i];
                loc = i;
            }
        }
    }
    return loc;
}

float calibrate_velocity(float velocity_mps)
{
    return calibration_state.fit_k * velocity_mps + calibration_state.fit_b;
}

float velocity_to_flow_rate(float velocity_mps)
{
    float radius_m = PIPE_DIAMETER_M * 0.5f;
    float area_m2 = PI * radius_m * radius_m;
    return area_m2 * velocity_mps;
}

static float median_velocity_update(float value)
{
    float a;
    float b;
    float c;

    velocity_history[velocity_history_index] = value;
    velocity_history_index = (velocity_history_index + 1) % 3;
    if (velocity_history_count < 3) {
        velocity_history_count++;
        return value;
    }

    a = velocity_history[0];
    b = velocity_history[1];
    c = velocity_history[2];
    if (a > b) {
        float temp = a;
        a = b;
        b = temp;
    }
    if (b > c) {
        float temp = b;
        b = c;
        c = temp;
    }
    if (a > b) {
        b = a;
    }
    return b;
}

static void reset_algorithm_stability(void)
{
    covariance_history_valid = 0;
    velocity_history_count = 0;
    velocity_history_index = 0;
}

void init_window(void) {
    float denom = (float)(FFT_VALID_SAMPLES - 1);
    for (int i = 0; i < FFT_VALID_SAMPLES; i++) {
        float x = (2.0f * PI * i) / denom;
        window[i] = 0.5f * (1.0f - cosf(x));
    }
    for (int i = FFT_VALID_SAMPLES; i < FFT_SIZE; i++) {
        window[i] = 0.0f;
    }
}

void fft_4096(complex_t *x) {
    int i, j;
    for (i = 1, j = 0; i < FFT_SIZE; i++) {
        int bit = FFT_SIZE >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            complex_t t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }
    for (int len = 2; len <= FFT_SIZE; len <<= 1) {
        float ang = -2.0f * PI / len;
        float wr = cosf(ang), wi = sinf(ang);
        for (i = 0; i < FFT_SIZE; i += len) {
            float ur = 1.0f, ui = 0.0f;
            for (j = 0; j < len / 2; j++) {
                complex_t a = x[i + j];
                complex_t b = x[i + j + len / 2];
                float vr = b.real * ur - b.imag * ui;
                float vi = b.real * ui + b.imag * ur;
                x[i + j].real = a.real + vr;
                x[i + j].imag = a.imag + vi;
                x[i + j + len / 2].real = a.real - vr;
                x[i + j + len / 2].imag = a.imag - vi;
                float tmp = ur;
                ur = tmp * wr - ui * wi;
                ui = tmp * wi + ui * wr;
            }
        }
    }

}

void process_fft(void) {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {

        tcp_stream_poll();

        for (int blk = 0; blk < NUM_FFT_BLOCKS; blk++) {
            int base = blk * FFT_STEP_SAMPLES;
            for (int n = 0; n < FFT_SIZE; n++) {
                if (n < FFT_VALID_SAMPLES) {
                    int idx = (base + n) * NUM_CHANNELS + ch;
                    buf[n].real = adc_buffer[idx] * window[n];
                } else {
                    buf[n].real = 0.0f;
                }
                buf[n].imag = 0.0f;
            }
            fft_4096(buf);
            for (int k = 0; k < FFT_SIZE; k++) {
                fft_out[blk][ch][k] = buf[k];
            }
        }
    }
}

void compute_cov(void) {
    for (int k = 0; k < NUM_FREQ; k++) {

        tcp_stream_poll();

        for (int i = 0; i < NUM_CHANNELS; i++) {
            for (int j = 0; j < NUM_CHANNELS; j++) {
                R_all[k][i][j].real = 0.0f;
                R_all[k][i][j].imag = 0.0f;
            }
        }
        for (int blk = 0; blk < NUM_FFT_BLOCKS; blk++) {
            for (int i = 0; i < NUM_CHANNELS; i++) {
                complex_t xi = fft_out[blk][i][k];
                for (int j = 0; j < NUM_CHANNELS; j++) {
                    complex_t xj = fft_out[blk][j][k];
                    float real = xi.real * xj.real + xi.imag * xj.imag;
                    float imag = xi.imag * xj.real - xi.real * xj.imag;
                    R_all[k][i][j].real += real;
                    R_all[k][i][j].imag += imag;
                }
            }
        }
        for (int i = 0; i < NUM_CHANNELS; i++) {
            for (int j = 0; j < NUM_CHANNELS; j++) {
                R_all[k][i][j].real /= NUM_FFT_BLOCKS;
                R_all[k][i][j].imag /= NUM_FFT_BLOCKS;

                if (covariance_history_valid) {
                    R_all[k][i][j].real =
                        COV_CURRENT_WEIGHT * R_all[k][i][j].real +
                        (1.0f - COV_CURRENT_WEIGHT) * R_history[k][i][j].real;
                    R_all[k][i][j].imag =
                        COV_CURRENT_WEIGHT * R_all[k][i][j].imag +
                        (1.0f - COV_CURRENT_WEIGHT) * R_history[k][i][j].imag;
                }
                R_history[k][i][j] = R_all[k][i][j];
            }
        }

        {
            float trace = 0.0f;
            float loading;

            for (int i = 0; i < NUM_CHANNELS; ++i) {
                trace += R_all[k][i][i].real;
            }
            loading = COV_DIAGONAL_LOADING *
                      fmaxf(trace, EPS) / (float)NUM_CHANNELS;
            for (int i = 0; i < NUM_CHANNELS; ++i) {
                R_all[k][i][i].real += loading;
            }
        }
    }
    covariance_history_valid = 1;
}

void complex_to_real_symmetric(complex_t R[NUM_CHANNELS][NUM_CHANNELS], float A[16][16]) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        for (int j = 0; j < NUM_CHANNELS; j++) {
            A[i][j] = R[i][j].real;
            A[i][j + NUM_CHANNELS] = -R[i][j].imag;
            A[i + NUM_CHANNELS][j] = R[i][j].imag;
            A[i + NUM_CHANNELS][j + NUM_CHANNELS] = R[i][j].real;
        }
    }
}

void jacobi_evd(float A[16][16], float V[16][16]) {
    int n = 16;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            V[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    for (int iter = 0; iter < 10000; iter++) {
        int p = 0, q = 1;
        float max = fabsf(A[p][q]);
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (fabsf(A[i][j]) > max) {
                    max = fabsf(A[i][j]);
                    p = i;
                    q = j;
                }
            }
        }
        if (max < 1e-8f) break;
        float theta = 0.5f * (A[q][q] - A[p][p]) / A[p][q];
        float t = (theta >= 0 ? 1.0f : -1.0f) / (fabsf(theta) + sqrtf(theta * theta + 1.0f));
        float c = 1.0f / sqrtf(t * t + 1.0f);
        float s = t * c;
        float app = A[p][p];
        float aqq = A[q][q];
        float apq = A[p][q];
        A[p][p] = app - t * apq;
        A[q][q] = aqq + t * apq;
        A[p][q] = 0.0f;
        A[q][p] = 0.0f;
        for (int j = 0; j < n; j++) {
            if (j != p && j != q) {
                float apj = A[p][j];
                float aqj = A[q][j];
                A[p][j] = c * apj - s * aqj;
                A[j][p] = A[p][j];
                A[q][j] = c * aqj + s * apj;
                A[j][q] = A[q][j];
            }
        }
        for (int j = 0; j < n; j++) {
            float vpj = V[j][p];
            float vqj = V[j][q];
            V[j][p] = c * vpj - s * vqj;
            V[j][q] = c * vqj + s * vpj;
        }

    }
}

void compute_music(void) {
    float eigvec[16][16];
    float A[16][16];
    const int steering_sign = steering_vector_sign;
    for (int w = 0; w < NUM_FREQ; w++) {

        tcp_stream_poll();

        complex_to_real_symmetric(R_all[w], A);
        jacobi_evd(A, eigvec);
        float eig[8];
        for (int i = 0; i < 8; i++) eig[i] = A[i][i];
        for (int i = 0; i < 7; i++) {
            for (int j = i + 1; j < 8; j++) {
                if (eig[i] < eig[j]) {
                    float tmp = eig[i];
                    eig[i] = eig[j];
                    eig[j] = tmp;
                    for (int k = 0; k < 16; k++) {
                        float t = eigvec[k][i];
                        eigvec[k][i] = eigvec[k][j];
                        eigvec[k][j] = t;
                        t = eigvec[k][i + 8];
                        eigvec[k][i + 8] = eigvec[k][j + 8];
                        eigvec[k][j + 8] = t;
                    }
                }
            }
        }
        int noise_dim = NUM_CHANNELS - 6;
        float E_noise[NUM_CHANNELS - 6][NUM_CHANNELS * 2];
        for (int i = 0; i < noise_dim; i++) {
            for (int j = 0; j < NUM_CHANNELS; j++) {
                E_noise[i][j] = eigvec[j][i + NUM_CHANNELS - noise_dim];
                E_noise[i][j + NUM_CHANNELS] = eigvec[NUM_CHANNELS + j][i + NUM_CHANNELS - noise_dim];
            }
        }
        float k = 0.0f;
        float step = 0.25f;
        for (int k_idx = 0; k_idx < NUM_K; k_idx++) {
            float a_real[2 * NUM_CHANNELS];
            for (int i = 0; i < NUM_CHANNELS; i++) {
                float phase = (float)steering_sign * k * proc_positions[i];
                a_real[i] = cosf(phase);
                a_real[i + NUM_CHANNELS] = sinf(phase);
            }
            float sum = 0.0f;
            for (int n = 0; n < noise_dim; n++) {
                float dot_r = 0.0f;
                float dot_i = 0.0f;
                for (int i = 0; i < NUM_CHANNELS; i++) {
                    float er = E_noise[n][i];
                    float ei = E_noise[n][i + NUM_CHANNELS];
                    float ar = a_real[i];
                    float ai = a_real[i + NUM_CHANNELS];
                    dot_r += er * ar + ei * ai;
                    dot_i += er * ai - ei * ar;
                }
                sum += dot_r * dot_r + dot_i * dot_i;
            }
            if (sum < EPS) sum = EPS;
            P_music[w][k_idx] = 1.0f / sum;
            k += step;
        }
    }
}

void compute_P_db(void) {
    for (int fi = 0; fi < NUM_FREQ; fi++) {
        for (int ki = 0; ki < NUM_K; ki++) {
            float val = fabsf(P_music[fi][ki]);
            if (val < 1e-12f) val = 1e-12f;
            P_db[fi][ki] = 10.0f * log10f(val);
        }
    }
}

void compute_energy_uopt(void) {
    for (int ui = 0; ui < NUM_U; ui++) {
        float u = u_trials[ui];
        float le = 0.0f;
        int cnt = 0;
        for (int fi = 0; fi < NUM_FREQ; fi++) {
            float k_line = 2.0f * PI * f_axis[fi] / u;
            if (k_line >= k_axis[0] && k_line <= k_axis[NUM_K - 1]) {
                float val = interp1(k_axis, P_db[fi], NUM_K, k_line);
                le += val;
                cnt++;
            }
        }
        energy[ui] = (cnt > 0) ? (le / cnt) : 0.0f;
    }
    float en_s[NUM_U];
    movmean(energy, NUM_U, MOVMEAN_WINDOW, en_s);
    int loc = find_peak_index(en_s, NUM_U);
    float u_peak = u_trials[loc];
    float half_width = fminf(u_peak * PEAK_AVG_RANGE_RATIO, PEAK_AVG_RANGE_MAX);
    float uf_start = fmaxf(u_peak - half_width, u_range[0]);
    float uf_end = fminf(u_peak + half_width, u_range[1]);
    float area = 0.0f;
    float weighted_area = 0.0f;

    u_peak_raw = u_peak;
    u_peak_avg_start = uf_start;
    u_peak_avg_end = uf_end;

    for (int ui = 0; ui < (NUM_U - 1); ui++) {
        float x0 = u_trials[ui];
        float x1 = u_trials[ui + 1];

        if ((x1 <= uf_start) || (x0 >= uf_end)) {
            continue;
        }

        float xa = fmaxf(x0, uf_start);
        float xb = fminf(x1, uf_end);
        float ya = interp1(u_trials, en_s, NUM_U, xa);
        float yb = interp1(u_trials, en_s, NUM_U, xb);
        float dx = xb - xa;
        float segment_area = 0.5f * (ya + yb) * dx;
        float segment_weighted_area = 0.5f * (xa * ya + xb * yb) * dx;

        area += segment_area;
        weighted_area += segment_weighted_area;
    }

    u_peak_avg_area = area;
    u_peak_avg_weighted_area = weighted_area;

    if (fabsf(area) > EPS) {
        u_opt = weighted_area / area;
    } else {
        u_opt = u_peak;
    }

    u_opt = median_velocity_update(u_opt);
    u_opt_raw = u_opt;
    external_flow_set_raw_rate(velocity_to_flow_rate(u_opt_raw));
    u_opt = calibrate_velocity(u_opt_raw);
    flow_rate_m3s = velocity_to_flow_rate(u_opt);
    flow_pulse_set_rate(flow_rate_m3s);
}

static void reset_measurements(void)
{
    flow_pulse_reset_total();
    external_flow_reset();
}

static void run_acquisition_loop(void)
{
    uint64_t last_processed_sample = 0U;
    uint64_t last_control_poll_ms = 0U;

    reset_algorithm_stability();
    if (ad7606_dma_start() != XST_SUCCESS) {
        return;
    }

    while (1) {
        uint64_t now_ms;

#if ENABLE_TCP_STREAM
        tcp_stream_poll();
#endif
        external_flow_update();

        now_ms = get_time_ms();
        if (last_control_poll_ms == 0U ||
            now_ms - last_control_poll_ms >= CAL_CONTROL_POLL_MS) {
            last_control_poll_ms = now_ms;
            if (rs485_modbus_poll_reset_request()) {
                reset_measurements();
                (void)rs485_modbus_write_measurements(
                    u_opt_raw,
                    u_opt,
                    flow_rate_m3s,
                    flow_pulse_total_volume_m3());
                (void)rs485_modbus_clear_reset_request();
            }
            if (calibration_poll_manual_apply()) {
                u_opt = calibrate_velocity(u_opt_raw);
                flow_rate_m3s = velocity_to_flow_rate(u_opt);
                flow_pulse_set_rate(flow_rate_m3s);
                (void)rs485_modbus_write_measurements(
                    u_opt_raw,
                    u_opt,
                    flow_rate_m3s,
                    flow_pulse_total_volume_m3());
            }
            pulse_config_sync();
            calibration_poll_controls();
            gain_poll_controls();
        }
        calibration_refresh_collecting_status();

        if (ad7606_dma_has_error()) {
            flow_pulse_abort();
#if ENABLE_TCP_STREAM
            tcp_stream_reset_queue();
#endif
            reset_algorithm_stability();
            last_processed_sample = 0U;
            if (ad7606_dma_recover() != XST_SUCCESS) {
                return;
            }
            continue;
        }

        (void)ad7606_dma_ensure_running();
#if ENABLE_TCP_STREAM
        ad7606_dma_service_completed(tcp_stream_enqueue);
#else
        ad7606_dma_service_completed(NULL);
#endif

        {
            uint64_t decimated_samples = ad7606_dma_decimated_total_samples();

            if (ad7606_dma_decimated_samples() >= RAW_WINDOW_SAMPLES_PER_CH &&
                (last_processed_sample == 0U ||
                 (decimated_samples - last_processed_sample) >= FLOW_UPDATE_SAMPLES) &&
                ad7606_dma_copy_latest_window(adc_buffer) == XST_SUCCESS) {
                last_processed_sample = decimated_samples;

                process_fft();
                compute_cov();
                compute_music();
                compute_P_db();
                compute_energy_uopt();

                {
                    ExternalFlowStats stats;
                    uint16_t saved_point;

                    external_flow_get_stats(&stats);
                    calibration_state.standard_velocity =
                        standard_velocity_from_flow_l_min(
                            stats.standard_flow_l_min);
                    saved_point = calibration_add_sample(
                        u_opt_raw,
                        calibration_state.standard_velocity);
                    if (saved_point != 0U) {
                        calibration_write_saved_point(saved_point);
                        if (calibration_state.target_points > 0U &&
                            calibration_state.point_count >=
                            calibration_state.target_points) {
                            calibration_fit_and_clear_setup();
                        }
                    }
                }

                (void)rs485_modbus_write_measurements(
                    u_opt_raw,
                    u_opt,
                    flow_rate_m3s,
                    flow_pulse_total_volume_m3());
                if (calibration_state.collecting) {
                    calibration_write_status();
                }
            }
        }
    }
}

int main(void)
{
    int status;

    init_platform();

    u_opt = 0.0f;
    u_opt_raw = 0.0f;
    u_peak_raw = 0.0f;
    u_peak_avg_start = 0.0f;
    u_peak_avg_end = 0.0f;
    u_peak_avg_area = 0.0f;
    u_peak_avg_weighted_area = 0.0f;
    flow_rate_m3s = 0.0f;
    steering_vector_sign = 1;
    calibration_init();
    calibration_flash_restore_coefficients();

    init_window();
    linspace(0.5f, 8.0f, NUM_U, u_trials);
    linspace(0.0f, FREQ_AXIS_MAX, NUM_FREQ, f_axis);
    linspace(0.0f, 100.0f, NUM_K, k_axis);

    status = rs485_modbus_init();
    if (status != XST_SUCCESS) {
        cleanup_platform();
        return -1;
    }

    calibration_write_status();

    status = interrupt_controller_init(&interrupt_controller);
    if (status != XST_SUCCESS) {
        cleanup_platform();
        return -1;
    }

    status = ad7606_dma_init(&interrupt_controller);
    if (status != XST_SUCCESS) {
        cleanup_platform();
        return -1;
    }

    status = flow_pulse_init(&interrupt_controller, rs485_modbus_gpio());
    if (status != XST_SUCCESS) {
        cleanup_platform();
        return -1;
    }
    pulse_config_sync();

    status = external_flow_init(&interrupt_controller, rs485_modbus_gpio());
    if (status != XST_SUCCESS) {
        cleanup_platform();
        return -1;
    }
    external_flow_set_calibration_factor(calibration_state.fit_k);

    status = gain_init();
    if (status != XST_SUCCESS) {
        cleanup_platform();
        return -1;
    }

#if ENABLE_TCP_STREAM
    /* Ethernet failure is non-fatal: acquisition and RS485 must keep running. */
    (void)tcp_stream_init();
#endif

    run_acquisition_loop();
    cleanup_platform();
    return 0;
}



