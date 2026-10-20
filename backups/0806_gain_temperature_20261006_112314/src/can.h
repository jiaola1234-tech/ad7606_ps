#ifndef CAN_H
#define CAN_H

#include "xgpiops.h"
#include "xil_types.h"

/*
 * GPIO simulated-CAN link used by the gain-conditioning board.
 *
 * This is the single-wire MIO9 test protocol, not an ISO 11898 CAN frame.
 * The receiver is the STM32 PA11 SIM_CAN_GPIO_RX_TEST implementation.
 */
int can_init(XGpioPs *gpio);
int can_send_gain_level(u16 level);

#endif /* CAN_H */
