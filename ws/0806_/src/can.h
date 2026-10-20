#ifndef CAN_H
#define CAN_H

#include "xgpiops.h"
#include "xscugic.h"
#include "xil_types.h"

/*
 * GPIO simulated-CAN link used by the gain-conditioning board.
 *
 * GPIO protocol, not ISO 11898: MIO9 -> PA11 commands;
 * PA12 -> MIO10 temperature reports. See app_config.h for pin assignments.
 */
typedef struct {
    s16 centi_celsius;
    u16 status; /* 0: valid temperature; 1: fault, no report, or timeout */
    u32 reports;
    u32 rejected_frames;
} SimCanTemperature;

int can_init(XGpioPs *gpio, XScuGic *interrupt_controller);
int can_send_gain_level(u16 level);
u16 can_gain_total(u16 mode);
/* Called by the shared PS GPIO callback, without replacing its owner. */
void can_gpio_interrupt(u32 bank, u32 status);
void can_get_temperature(SimCanTemperature *temperature);

#endif /* CAN_H */
