#ifndef AD7606_DMA_H
#define AD7606_DMA_H

#include <stddef.h>
#include <stdint.h>

#include "xscugic.h"

typedef void (*Ad7606BlockHandler)(const void *data, size_t bytes);

int ad7606_dma_init(XScuGic *interrupt_controller);
int ad7606_dma_start(void);
int ad7606_dma_recover(void);
void ad7606_dma_service_completed(Ad7606BlockHandler handler);
int ad7606_dma_ensure_running(void);
int ad7606_dma_copy_latest_window(float *destination);
int ad7606_dma_has_error(void);
int ad7606_dma_total_blocks(void);
int ad7606_dma_valid_blocks(void);
int ad7606_dma_decimated_samples(void);
uint64_t ad7606_dma_decimated_total_samples(void);

#endif
