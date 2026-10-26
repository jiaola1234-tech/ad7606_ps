#ifndef CAN_H
#define CAN_H

#include "xgpiops.h"
#include "xil_types.h"

/*
 * GPIO simulated-CAN link used by the gain-conditioning board.
 *
 * GPIO protocol, not ISO 11898: MIO9 -> PA11 commands.
 */

int can_init(XGpioPs *gpio);
int can_send_gain_level(u16 level);
u16 can_gain_total(u16 mode);

#endif /* CAN_H */
