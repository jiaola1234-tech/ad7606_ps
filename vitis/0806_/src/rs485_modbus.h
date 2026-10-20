#ifndef RS485_MODBUS_H
#define RS485_MODBUS_H

#include "xgpiops.h"
#include "xil_types.h"

typedef struct {
    u16 target_points;
    u16 timing_mode;
    u16 sample_time_s;
    u16 start_command;
    u16 stop_command;
    u16 fit_command;
    u16 clear_points_command;
} Rs485CalibrationControls;

int rs485_modbus_init(void);
XGpioPs *rs485_modbus_gpio(void);
int rs485_modbus_write_measurements(float raw_velocity,
                                    float calibrated_velocity,
                                    float flow_rate_m3s,
                                    float total_volume_m3);
int rs485_modbus_poll_reset_request(void);
int rs485_modbus_clear_reset_request(void);
int rs485_modbus_read_calibration_controls(Rs485CalibrationControls *controls);
int rs485_modbus_clear_calibration_start(void);
int rs485_modbus_clear_calibration_stop(void);
int rs485_modbus_clear_calibration_fit(void);
int rs485_modbus_clear_calibration_points_command(void);
int rs485_modbus_clear_calibration_curves(void);
int rs485_modbus_write_calibration_status(u16 point_count,
                                          u16 elapsed_time_s,
                                          float standard_velocity,
                                          float raw_average,
                                          float standard_average,
                                          float fit_k,
                                          float fit_b,
                                          u16 calibration_status);
int rs485_modbus_write_calibration_point(u16 point_index,
                                         float raw_average,
                                         float standard_average);

#endif
