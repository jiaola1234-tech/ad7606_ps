#ifndef TCP_STREAM_H
#define TCP_STREAM_H

#include <stddef.h>

#include "xil_types.h"

int tcp_stream_init(void);
void tcp_stream_poll(void);
void tcp_stream_reset_queue(void);
void tcp_stream_enqueue(const void *data, size_t bytes);
u32 tcp_stream_drop_count(void);

#endif

