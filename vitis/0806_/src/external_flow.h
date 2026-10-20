#ifndef EXTERNAL_FLOW_H
#define EXTERNAL_FLOW_H

#include "xgpiops.h"
#include "xil_types.h"
#include "xscugic.h"

typedef struct {
    u32 pulse_total;
    u32 rejected_pulses;
    float standard_flow_l_min;
    float standard_total_liter;
    float raw_total_liter;
    float calibration_factor;
} ExternalFlowStats;

int external_flow_init(XScuGic *interrupt_controller, XGpioPs *gpio);
void external_flow_reset(void);
void external_flow_set_raw_rate(float flow_rate_m3s);
void external_flow_update(void);
void external_flow_get_stats(ExternalFlowStats *stats);

#endif

