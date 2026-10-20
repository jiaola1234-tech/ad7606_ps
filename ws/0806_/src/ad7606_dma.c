#include "ad7606_dma.h"

#include <stdint.h>

#include "ad7606_sample.h"
#include "app_config.h"
#include "can.h"
#include "xaxidma.h"
#include "xil_cache.h"
#include "xil_exception.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xtime_l.h"

#define DMA_DEVICE_ID  XPAR_AXIDMA_0_DEVICE_ID
#define DMA_INTERRUPT  XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR
#define AD7606_BASE     XPAR_AD7606_SAMPLE_0_S00_AXI_BASEADDR
#define AD7606_START    AD7606_SAMPLE_S00_AXI_SLV_REG0_OFFSET
#define AD7606_LENGTH   AD7606_SAMPLE_S00_AXI_SLV_REG1_OFFSET

enum {
    RAW_FREE = 0,
    RAW_FILLING,
    RAW_DONE
};

static XAxiDma dma;
static int16_t raw_blocks[RAW_DMA_BUF_NUM][DMA_BLOCK_SAMPLES] __attribute__((aligned(64)));
static volatile int error_flag;
static volatile int overrun_flag;
static volatile int active_index = -1;
static volatile int raw_state[RAW_DMA_BUF_NUM];
static volatile int done_queue[RAW_DMA_BUF_NUM];
static volatile int done_head;
static volatile int done_tail;
static volatile int done_count;
static volatile int total_blocks;
static volatile int write_sequence;
static volatile int valid_blocks;
static volatile u16 capture_gain_mode = GAIN_INITIAL_MODE;
static volatile XTime gain_settle_until;
static u16 block_gain[RAW_DMA_BUF_NUM];
static volatile int block_stable[RAW_DMA_BUF_NUM];
static volatile XTime block_capture_ticks[RAW_DMA_BUF_NUM];
static float last_stable_input[NUM_CHANNELS];

/* The ADC and TCP stream stay at 50 kHz.  Only the flow algorithm uses this
 * filtered 2 kHz ring buffer. */
static float decimated_ring[RAW_WINDOW_SAMPLES_PER_CH][NUM_CHANNELS];
static float decimator_state[NUM_CHANNELS][DECIMATION_FILTER_STAGES];
static unsigned int decimation_phase;
static unsigned int decimated_write_index;
static unsigned int decimated_valid_samples;
static uint64_t decimated_total_samples;

static void decimate_block(const int16_t *source, u16 mode, int stable)
{
    int sample;
    /* Keep a common gain-5 amplitude reference across the long MUSIC window.
     * TCP continues to carry unmodified ADC samples. */
    float scale = (float)can_gain_total(GAIN_INITIAL_MODE) /
                  (float)can_gain_total(mode);

    for (sample = 0; sample < DMA_SAMPLES_PER_CH; ++sample) {
        float filtered[NUM_CHANNELS];
        int channel;

        for (channel = 0; channel < NUM_CHANNELS; ++channel) {
            float value = last_stable_input[channel];
            int stage;

            if (stable) {
                value = (float)source[sample * NUM_CHANNELS + channel] * scale;
                last_stable_input[channel] = value;
            }

            for (stage = 0; stage < DECIMATION_FILTER_STAGES; ++stage) {
                float state = decimator_state[channel][stage];
                state += DECIMATION_FILTER_ALPHA * (value - state);
                decimator_state[channel][stage] = state;
                value = state;
            }
            filtered[channel] = value;
        }

        decimation_phase++;
        if (decimation_phase >= DECIMATION_FACTOR) {
            decimation_phase = 0U;
            for (channel = 0; channel < NUM_CHANNELS; ++channel) {
                decimated_ring[decimated_write_index][channel] = filtered[channel];
            }
            decimated_write_index++;
            if (decimated_write_index >= RAW_WINDOW_SAMPLES_PER_CH) {
                decimated_write_index = 0U;
            }
            if (decimated_valid_samples < RAW_WINDOW_SAMPLES_PER_CH) {
                decimated_valid_samples++;
            }
            decimated_total_samples++;
        }
    }
}

static void reset_state(void)
{
    int index;
    int channel;
    int stage;

    error_flag = 0;
    overrun_flag = 0;
    active_index = -1;
    done_head = 0;
    done_tail = 0;
    done_count = 0;
    total_blocks = 0;
    write_sequence = 0;
    valid_blocks = 0;
    decimation_phase = 0U;
    decimated_write_index = 0U;
    decimated_valid_samples = 0U;
    decimated_total_samples = 0U;

    for (channel = 0; channel < NUM_CHANNELS; ++channel) {
        last_stable_input[channel] = 0.0f;
        for (stage = 0; stage < DECIMATION_FILTER_STAGES; ++stage) {
            decimator_state[channel][stage] = 0.0f;
        }
    }

    for (index = 0; index < RAW_DMA_BUF_NUM; ++index) {
        raw_state[index] = RAW_FREE;
        block_stable[index] = 0;
        done_queue[index] = 0;
    }
}

static int start_transfer(int index)
{
    size_t bytes = (size_t)DMA_BLOCK_SAMPLES * ADC_BYTE;
    int status;
    XTime now;

    XTime_GetTime(&now);
    block_gain[index] = capture_gain_mode;
    block_stable[index] = now >= gain_settle_until;

    Xil_DCacheFlushRange((UINTPTR)raw_blocks[index], bytes);
    status = XAxiDma_SimpleTransfer(&dma,
                                    (UINTPTR)raw_blocks[index],
                                    bytes,
                                    XAXIDMA_DEVICE_TO_DMA);
    if (status != XST_SUCCESS) {
        return status;
    }

    AD7606_SAMPLE_mWriteReg(AD7606_BASE, AD7606_LENGTH, DMA_SAMPLES_PER_CH);
    AD7606_SAMPLE_mWriteReg(AD7606_BASE, AD7606_START, 1);
    return XST_SUCCESS;
}

static int start_next_transfer(void)
{
    int next_index = write_sequence % RAW_DMA_BUF_NUM;

    if (raw_state[next_index] != RAW_FREE) {
        active_index = -1;
        overrun_flag = 1;
        return XST_FAILURE;
    }

    raw_state[next_index] = RAW_FILLING;
    active_index = next_index;
    if (start_transfer(next_index) != XST_SUCCESS) {
        raw_state[next_index] = RAW_FREE;
        active_index = -1;
        error_flag = 1;
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static void dma_interrupt_handler(void *callback)
{
    XAxiDma *instance = (XAxiDma *)callback;
    u32 irq_status = XAxiDma_IntrGetIrq(instance, XAXIDMA_DEVICE_TO_DMA);

    XAxiDma_IntrAckIrq(instance, irq_status, XAXIDMA_DEVICE_TO_DMA);
    if ((irq_status & XAXIDMA_IRQ_ERROR_MASK) != 0U) {
        error_flag = 1;
        return;
    }

    if ((irq_status & XAXIDMA_IRQ_IOC_MASK) != 0U) {
        int completed_index = active_index;
        total_blocks++;

        if (completed_index >= 0) {
            XTime captured;
            XTime_GetTime(&captured);
            block_capture_ticks[completed_index] = captured;
            raw_state[completed_index] = RAW_DONE;
            if (done_count < RAW_DMA_BUF_NUM) {
                done_queue[done_tail] = completed_index;
                done_tail = (done_tail + 1) % RAW_DMA_BUF_NUM;
                done_count++;
            } else {
                overrun_flag = 1;
            }

            write_sequence = (completed_index + 1) % RAW_DMA_BUF_NUM;
            if (valid_blocks < RAW_DMA_BUF_NUM) {
                valid_blocks++;
            }
        }
        (void)start_next_transfer();
    }
}

int ad7606_dma_init(XScuGic *interrupt_controller)
{
    XAxiDma_Config *config;
    int status;

    if (interrupt_controller == NULL) {
        return XST_FAILURE;
    }

    config = XAxiDma_LookupConfig(DMA_DEVICE_ID);
    if (config == NULL) {
        return XST_FAILURE;
    }

    status = XAxiDma_CfgInitialize(&dma, config);
    if (status != XST_SUCCESS || XAxiDma_HasSg(&dma)) {
        return XST_FAILURE;
    }

    XAxiDma_Reset(&dma);
    while (!XAxiDma_ResetIsDone(&dma)) {
    }
    XAxiDma_IntrDisable(&dma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrEnable(&dma,
                       (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK),
                       XAXIDMA_DEVICE_TO_DMA);

    status = XScuGic_Connect(interrupt_controller,
                             DMA_INTERRUPT,
                             (Xil_ExceptionHandler)dma_interrupt_handler,
                             &dma);
    if (status != XST_SUCCESS) {
        return status;
    }
    XScuGic_Enable(interrupt_controller, DMA_INTERRUPT);
    reset_state();
    return XST_SUCCESS;
}

int ad7606_dma_start(void)
{
    return start_next_transfer();
}

int ad7606_dma_recover(void)
{
    XAxiDma_Reset(&dma);
    while (!XAxiDma_ResetIsDone(&dma)) {
    }

    XAxiDma_IntrDisable(&dma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrEnable(&dma,
                       (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK),
                       XAXIDMA_DEVICE_TO_DMA);
    reset_state();
    return start_next_transfer();
}

void ad7606_dma_gain_begin_change(void)
{
    XTime now;
    XTime_GetTime(&now);
    Xil_ExceptionDisable();
    gain_settle_until = now + (uint64_t)COUNTS_PER_SECOND;
    if (active_index >= 0) {
        block_stable[active_index] = 0;
    }
    Xil_ExceptionEnable();
}

void ad7606_dma_set_gain(u16 mode)
{
    XTime now;
    if (can_gain_total(mode) == 0U) {
        return;
    }
    XTime_GetTime(&now);
    Xil_ExceptionDisable();
    capture_gain_mode = mode;
    gain_settle_until = now +
                       ((uint64_t)COUNTS_PER_SECOND * GAIN_SETTLE_MS / 1000ULL);
    if (active_index >= 0) {
        block_stable[active_index] = 0;
    }
    Xil_ExceptionEnable();
}

void ad7606_dma_service_completed(Ad7606BlockHandler handler,
                                  Ad7606GainBlockHandler gain_handler)
{
    size_t bytes = (size_t)DMA_BLOCK_SAMPLES * ADC_BYTE;

    while (done_count > 0) {
        int index;

        Xil_ExceptionDisable();
        index = done_queue[done_head];
        done_head = (done_head + 1) % RAW_DMA_BUF_NUM;
        done_count--;
        Xil_ExceptionEnable();

        Xil_DCacheInvalidateRange((UINTPTR)raw_blocks[index], bytes);
        if (gain_handler != NULL) {
            gain_handler(raw_blocks[index], block_gain[index],
                         block_stable[index], block_capture_ticks[index]);
        }
        decimate_block(raw_blocks[index], block_gain[index], block_stable[index]);
        if (handler != NULL) {
            handler(raw_blocks[index], bytes);
        }
        raw_state[index] = RAW_FREE;
    }
}

int ad7606_dma_ensure_running(void)
{
    int index;

    if (active_index >= 0) {
        return XST_SUCCESS;
    }
    for (index = 0; index < RAW_DMA_BUF_NUM; ++index) {
        if (raw_state[index] == RAW_FREE) {
            overrun_flag = 0;
            return start_next_transfer();
        }
    }
    return XST_FAILURE;
}

int ad7606_dma_copy_latest_window(float *destination)
{
    unsigned int sample;
    unsigned int start;

    if (destination == NULL ||
        decimated_valid_samples < RAW_WINDOW_SAMPLES_PER_CH) {
        return XST_FAILURE;
    }

    start = decimated_write_index;

    for (sample = 0; sample < RAW_WINDOW_SAMPLES_PER_CH; ++sample) {
        unsigned int index = start + sample;
        int channel;

        if (index >= RAW_WINDOW_SAMPLES_PER_CH) {
            index -= RAW_WINDOW_SAMPLES_PER_CH;
        }
        for (channel = 0; channel < NUM_CHANNELS; ++channel) {
            destination[sample * NUM_CHANNELS + channel] =
                decimated_ring[index][channel];
        }
    }
    return XST_SUCCESS;
}

int ad7606_dma_has_error(void)
{
    return error_flag;
}

int ad7606_dma_total_blocks(void)
{
    return total_blocks;
}

int ad7606_dma_valid_blocks(void)
{
    return valid_blocks;
}

int ad7606_dma_decimated_samples(void)
{
    return (int)decimated_valid_samples;
}

uint64_t ad7606_dma_decimated_total_samples(void)
{
    return decimated_total_samples;
}
