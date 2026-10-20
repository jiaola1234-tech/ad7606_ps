#include "interrupt_controller.h"

#include "xil_exception.h"
#include "xparameters.h"
#include "xstatus.h"

int interrupt_controller_init(XScuGic *controller)
{
    XScuGic_Config *config;
    int status;

    config = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
    if (config == NULL) {
        return XST_FAILURE;
    }

    status = XScuGic_CfgInitialize(controller, config, config->CpuBaseAddress);
    if (status != XST_SUCCESS) {
        return status;
    }

    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 controller);
    Xil_ExceptionEnable();
    return XST_SUCCESS;
}

