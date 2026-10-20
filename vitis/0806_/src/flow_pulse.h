#ifndef FLOW_PULSE_H
#define FLOW_PULSE_H

#include "xgpiops.h"
#include "xscugic.h"

int flow_pulse_init(XScuGic *interrupt_controller, XGpioPs *gpio);
void flow_pulse_set_rate(float flow_rate_m3s);
void flow_pulse_reset_total(void);
void flow_pulse_abort(void);
float flow_pulse_total_volume_m3(void);

#endif

