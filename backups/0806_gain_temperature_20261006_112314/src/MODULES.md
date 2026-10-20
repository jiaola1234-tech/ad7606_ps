# Firmware modules

## Main algorithm

- `main.c`: application startup, acquisition loop, FFT, temporally smoothed and diagonally loaded covariance matrix, MUSIC spectrum, robust velocity estimate, calibration and flow-rate calculation.
- `app_config.h`: 50 kHz ADC / 2 kHz algorithm sampling, decimation, FFT, MUSIC, stability, calibration, DMA-buffer and TCP-block constants.
  `FLOW_STABILITY_PROFILE` selects one of four isolated window experiments; profile 1 (2.048 s small window, 16.384 s big window, 50% overlap) is the recommended default.

## Hardware and communication

- `ad7606_dma.c/.h`: AD7606 start registers, 50 kHz AXI DMA, raw-data ring buffers, four-pole anti-alias filtering, 25:1 algorithm decimation and error recovery. TCP transmission keeps the original 50 kHz samples.
- `tcp_stream.c/.h`: lwIP TCP server, network polling and raw ADC block transmission. Address: `192.168.1.10:5001`.
- `rs485_modbus.c/.h`: UART0, MIO41 RS485 direction control, Modbus display data and reset-button polling. Format: `9600 8O1`.
- `can.c/.h`: MIO9 single-wire simulated-CAN transmitter for gain stages 1-4. It sends the `A5 20 A3 level ~level CRC8` frame consumed by the STM32 PA11 test receiver.
- `flow_pulse.c/.h`: calculated-flow integration and MIO15 pulse output. One 20 ms pulse is generated per accumulated liter.
- `external_flow.c/.h`: MIO14 external pulse input, GPIO interrupt, TTC0 periodic calculation and comparison-flow statistics. Default: 600 pulses per liter.
- `interrupt_controller.c/.h`: shared PS GIC initialization. Feature modules connect their own interrupt handlers afterward.

## Gain-control startup and error behavior

- `gain_init()` initializes the MIO9 transmitter and sends the default level 4 before publishing the level to the display.
- `gain_apply_stage()` sends the GPIO frame before updating the display status register. If the level is invalid or the transmitter is not initialized, the display status is left at the previous level.
- The transmitter is a validated GPIO simulation protocol, not a PS CAN peripheral. The current Vivado platform has PS CAN0/CAN1 disabled; do not describe this link as a standard CAN bus in test reports.

## BSP support

- `platform.c/.h` and `platform_config.h`: Xilinx standalone platform and Ethernet initialization support.
- `lscript.ld`: Cortex-A9 linker script.
- `Xilinx.spec`: Xilinx GCC start-file specification.

Peripheral modules keep their driver objects and runtime state private. `main.c` communicates with them only through their public headers. The old header is retained as `adc_dma_legacy.h.disabled`; active DMA code uses `ad7606_dma.h`.
