#include "tcp_stream.h"

#include <string.h>

#include "app_config.h"
#include "lwip/err.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "netif/xadapter.h"
#include "xparameters.h"
#include "xstatus.h"

#define TCP_TX_QUEUE_DEPTH 16
#define TCP_SERVER_PORT    5001

static struct netif server_netif;
static struct tcp_pcb *connected_pcb;
static uint8_t tx_queue[TCP_TX_QUEUE_DEPTH][TCP_BLOCK_BYTES] __attribute__((aligned(64)));
static volatile u32 bytes_to_send;
static volatile u32 send_offset;
static volatile int queue_head;
static volatile int queue_tail;
static volatile int queue_count;
static volatile int active_slot = -1;
static volatile u32 drop_count;
static int netif_ready;

static err_t tcp_connection_recv(void *arg, struct tcp_pcb *tpcb,
                                 struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;
    if (p == NULL) {
        if (connected_pcb == tpcb) {
            connected_pcb = NULL;
            active_slot = -1;
            bytes_to_send = 0U;
            send_offset = 0U;
        }
        tcp_close(tpcb);
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void tcp_connection_error(void *arg, err_t err)
{
    struct tcp_pcb *pcb = (struct tcp_pcb *)arg;
    (void)arg;
    (void)err;
    if (connected_pcb == pcb) {
        connected_pcb = NULL;
        active_slot = -1;
        bytes_to_send = 0U;
        send_offset = 0U;
    }
}

static err_t tcp_accept_callback(void *arg, struct tcp_pcb *new_pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || new_pcb == NULL) {
        return ERR_VAL;
    }

    connected_pcb = new_pcb;
    new_pcb->flags |= TF_NODELAY;
    tcp_arg(new_pcb, new_pcb);
    tcp_recv(new_pcb, tcp_connection_recv);
    tcp_err(new_pcb, tcp_connection_error);
    return ERR_OK;
}

void tcp_stream_reset_queue(void)
{
    bytes_to_send = 0U;
    send_offset = 0U;
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    active_slot = -1;
    drop_count = 0U;
}

int tcp_stream_init(void)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gateway;
    struct tcp_pcb *pcb;
    static unsigned char mac_address[] = {0x00, 0x0a, 0x35, 0x00, 0x01, 0x02};

    netif_ready = 0;
    connected_pcb = NULL;

    IP4_ADDR(&ipaddr, 192, 168, 1, 10);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 1, 1);

    lwip_init();
    if (xemac_add(&server_netif, &ipaddr, &netmask, &gateway, mac_address,
                  XPAR_XEMACPS_0_BASEADDR) == NULL) {
        return XST_FAILURE;
    }

    netif_set_default(&server_netif);
    netif_set_up(&server_netif);
    netif_ready = 1;

    pcb = tcp_new();
    if (pcb == NULL) {
        return XST_FAILURE;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, TCP_SERVER_PORT) != ERR_OK) {
        tcp_close(pcb);
        return XST_FAILURE;
    }

    pcb = tcp_listen(pcb);
    if (pcb == NULL) {
        return XST_FAILURE;
    }
    tcp_accept(pcb, tcp_accept_callback);
    tcp_stream_reset_queue();
    return XST_SUCCESS;
}

void tcp_stream_poll(void)
{
    if (!netif_ready) {
        return;
    }

    xemacif_input(&server_netif);

    if (connected_pcb != NULL && active_slot < 0 && queue_count > 0) {
        active_slot = queue_head;
        send_offset = 0U;
        bytes_to_send = TCP_BLOCK_BYTES;
    }

    if (connected_pcb != NULL && active_slot >= 0 && bytes_to_send > 0U) {
        u16_t free_space = tcp_sndbuf(connected_pcb);
        if (free_space > 0U) {
            u16_t chunk = free_space < bytes_to_send ? free_space : (u16_t)bytes_to_send;
            err_t err = tcp_write(connected_pcb,
                                  tx_queue[active_slot] + send_offset,
                                  chunk,
                                  TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                tcp_output(connected_pcb);
                send_offset += chunk;
                bytes_to_send -= chunk;
            } else if (err == ERR_ABRT || err == ERR_RST || err == ERR_CLSD) {
                connected_pcb = NULL;
                active_slot = -1;
                bytes_to_send = 0U;
                send_offset = 0U;
            }
            }
        }
    }

    if (connected_pcb != NULL && active_slot >= 0 && bytes_to_send == 0U) {
        queue_head = (queue_head + 1) % TCP_TX_QUEUE_DEPTH;
        if (queue_count > 0) {
            queue_count--;
        }
        active_slot = -1;
    }
}

void tcp_stream_enqueue(const void *data, size_t bytes)
{
    if (connected_pcb == NULL || data == NULL) {
        return;
    }
    if (bytes > TCP_BLOCK_BYTES) {
        bytes = TCP_BLOCK_BYTES;
    }
    if (queue_count >= TCP_TX_QUEUE_DEPTH) {
        drop_count++;
        return;
    }

    memcpy(tx_queue[queue_tail], data, bytes);
    if (bytes < TCP_BLOCK_BYTES) {
        memset(tx_queue[queue_tail] + bytes, 0, TCP_BLOCK_BYTES - bytes);
    }
    queue_tail = (queue_tail + 1) % TCP_TX_QUEUE_DEPTH;
    queue_count++;
}

u32 tcp_stream_drop_count(void)
{
    return drop_count;
}
