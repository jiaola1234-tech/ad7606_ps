# Firmware modules

## Main algorithm

- `main.c`: application startup, acquisition loop, FFT, temporally smoothed and diagonally loaded covariance matrix, MUSIC spectrum, robust velocity estimate, calibration and flow-rate calculation.
- `app_config.h`: 50 kHz ADC / 2 kHz algorithm sampling, decimation, FFT, MUSIC, stability, calibration, DMA-buffer and TCP-block constants.
  `FLOW_STABILITY_PROFILE` selects one of four isolated window experiments; profile 1 (2.048 s small window, 16.384 s big window, 50% overlap) is the recommended default.

## Hardware and communication

- `ad7606_dma.c/.h`: AD7606 start registers, 50 kHz AXI DMA, raw-data ring buffers, four-pole anti-alias filtering, 25:1 algorithm decimation and error recovery. TCP transmission keeps the original 50 kHz samples.
- `tcp_stream.c/.h`: lwIP TCP server, network polling and raw ADC block transmission. Address: `192.168.1.10:5001`.
- `rs485_modbus.c/.h`: UART0, MIO41 RS485 direction control, Modbus display data and reset-button polling. Format: `9600 8O1`.
- `can.c/.h`: simulated-CAN gain modes 1-10 on MIO9 -> STM32 PA11, and temperature/fault reception on STM32 PA12 -> MIO10. RX uses TTC2 and the shared GPIO callback; this is not ISO CAN.
- `flow_pulse.c/.h`: calculated-flow integration and MIO15 pulse output. One 20 ms pulse is generated per accumulated liter.
- `external_flow.c/.h`: MIO14 external pulse input, GPIO interrupt, TTC0 periodic calculation and comparison-flow statistics. Default: 600 pulses per liter. Once the accumulated raw flow reaches 0.05 L, `calibration_factor` is also updated as `standard_total_liter / raw_total_liter` for comparison diagnostics; the active velocity result continues to use the fitted K/B calibration.
- `interrupt_controller.c/.h`: shared PS GIC initialization. Feature modules connect their own interrupt handlers afterward.

## Gain-control startup and error behavior

- `gain_init()` initializes the GPIO link and sends default mode 5 (100x), starting in manual mode.
- `gain_apply_stage()` sends the GPIO frame before updating the display status register. If the level is invalid or the transmitter is not initialized, the display status is left at the previous level.
- The transmitter is a validated GPIO simulation protocol, not a PS CAN peripheral. The current Vivado platform has PS CAN0/CAN1 disabled; do not describe this link as a standard CAN bus in test reports.
- The display uses 0140 current mode, 0141 target mode, 0142 apply command, 0143 signed centi-Celsius bits, 0144 temperature status, and 0145 auto command. Commands are cleared individually by Modbus function 06, with clear retries if the write echo is lost.
- Continuous auto is enabled by 0145=1 and stopped by manual 0142=1. Eight-channel demeaned RMS and absolute peak drive decisions; thresholds and timing are in `app_config.h`.
- DMA blocks retain capture-time gain tags. Flow input is normalized to mode-5 amplitude; unstable blocks hold the previous stable input before filtering. Raw ADC/TCP samples remain unchanged.
- See `../GAIN_TEMPERATURE_20261006.md` for wiring, registers, validation and board acceptance steps.

## BSP support

- `platform.c/.h` and `platform_config.h`: Xilinx standalone platform and Ethernet initialization support.
- `lscript.ld`: Cortex-A9 linker script.
- `Xilinx.spec`: Xilinx GCC start-file specification.

Peripheral modules keep their driver objects and runtime state private. `main.c` communicates with them only through their public headers. The old header is retained as `adc_dma_legacy.h.disabled`; active DMA code uses `ad7606_dma.h`.
