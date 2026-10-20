"""Run actual firmware communication/control code with deterministic GPIO/UART mocks.

Requires Python and a native GCC (e.g. C:/mingw64/bin/gcc.exe). Artifacts are
created in a temporary directory. This checks software behavior, not board timing.
"""
import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src"

MOCK_HEADER = r'''
#ifndef MOCK_BOARD_H
#define MOCK_BOARD_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef int16_t s16; typedef uintptr_t UINTPTR; typedef uint64_t XTime;
typedef void (*Xil_ExceptionHandler)(void *);
typedef struct { int unused; } XGpioPs;
typedef struct { int unused; } XScuGic;
typedef struct { uintptr_t BaseAddress; u32 InputClockHz; } XTtcPs_Config;
typedef struct { u16 interval; u32 status; int running; } XTtcPs;
typedef struct { uintptr_t BaseAddr; } XGpioPs_Config;
typedef struct { uintptr_t BaseAddress; } XUartPs_Config;
typedef struct { int unused; } XUartPs;
typedef struct { int unused; } XAxiDma;
typedef struct { int unused; } XAxiDma_Config;
typedef struct { u32 BaudRate, DataBits, Parity, StopBits; } XUartPsFormat;
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XST_INVALID_PARAM 15
#define COUNTS_PER_SECOND 1000000ULL
#define XPAR_XTTCPS_2_DEVICE_ID 2
#define XPAR_XTTCPS_2_INTR 44
#define XPAR_XUARTPS_0_DEVICE_ID 0
#define XPAR_XGPIOPS_0_DEVICE_ID 0
#define XPAR_AXIDMA_0_DEVICE_ID 0
#define XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR 61
#define XPAR_AD7606_SAMPLE_0_S00_AXI_BASEADDR 0
#define AD7606_SAMPLE_S00_AXI_SLV_REG0_OFFSET 0
#define AD7606_SAMPLE_S00_AXI_SLV_REG1_OFFSET 4
#define AD7606_SAMPLE_mWriteReg(a,b,c) ((void)0)
#define XAXIDMA_DEVICE_TO_DMA 1
#define XAXIDMA_IRQ_ERROR_MASK 2
#define XAXIDMA_IRQ_IOC_MASK 1
#define XAXIDMA_IRQ_ALL_MASK 3
#define XTTCPS_IXR_INTERVAL_MASK 1
#define XTTCPS_OPTION_INTERVAL_MODE 1
#define XTTCPS_OPTION_WAVE_DISABLE 2
#define XGPIOPS_IRQ_TYPE_EDGE_BOTH 3
#define XUARTPS_FORMAT_8_BITS 8
#define XUARTPS_FORMAT_ODD_PARITY 1
#define XUARTPS_FORMAT_1_STOP_BIT 1
#define xil_printf(...) ((void)0)
static XTime mock_now;
static u32 pins[128];
static u32 tx_values[1024];
static XTime tx_times[1024];
static int tx_count;
static u16 screen[200];
static u8 uart_response[64];
static u32 response_pos, response_length;
static int drop_clear_echo;
static int clear_writes;
static Xil_ExceptionHandler timer_handler;
static void *timer_callback;
static void XTime_GetTime(XTime *t) { *t = mock_now++; }
static void Xil_ExceptionDisable(void) {}
static void Xil_ExceptionEnable(void) {}
static void Xil_Out32(uintptr_t addr, u32 value) {(void)addr;(void)value;}
static u32 XGpioPs_ReadPin(XGpioPs *g, u32 p) {(void)g;return pins[p];}
static void XGpioPs_WritePin(XGpioPs *g, u32 p, u32 v) {
    (void)g; pins[p]=v;
    if(p==9 && tx_count<1024) {tx_values[tx_count]=v;tx_times[tx_count++]=mock_now;}
}
static void XGpioPs_SetDirectionPin(XGpioPs *g,u32 p,u32 v){(void)g;(void)p;(void)v;}
static void XGpioPs_SetOutputEnablePin(XGpioPs *g,u32 p,u32 v){(void)g;(void)p;(void)v;}
static void XGpioPs_IntrDisablePin(XGpioPs *g,u32 p){(void)g;(void)p;}
static void XGpioPs_IntrClearPin(XGpioPs *g,u32 p){(void)g;(void)p;}
static void XGpioPs_IntrEnablePin(XGpioPs *g,u32 p){(void)g;(void)p;}
static void XGpioPs_SetIntrTypePin(XGpioPs *g,u32 p,u32 v){(void)g;(void)p;(void)v;}
static XTtcPs_Config *XTtcPs_LookupConfig(u16 id){static XTtcPs_Config c={0,1000000};(void)id;return &c;}
static int XTtcPs_CfgInitialize(XTtcPs *t,XTtcPs_Config *c,uintptr_t a){(void)c;(void)a;memset(t,0,sizeof(*t));return 0;}
static void XTtcPs_Stop(XTtcPs *t){t->running=0;}
static void XTtcPs_Start(XTtcPs *t){t->running=1;}
static void XTtcPs_SetOptions(XTtcPs *t,u32 v){(void)t;(void)v;}
static void XTtcPs_CalcIntervalFromFreq(XTtcPs *t,u32 f,u16 *i,u8 *p){(void)t;*i=1000000/f;*p=16;}
static void XTtcPs_SetInterval(XTtcPs *t,u16 i){t->interval=i;}
static void XTtcPs_SetPrescaler(XTtcPs *t,u8 p){(void)t;(void)p;}
static void XTtcPs_EnableInterrupts(XTtcPs *t,u32 v){(void)t;(void)v;}
static void XTtcPs_ResetCounterValue(XTtcPs *t){(void)t;}
static u32 XTtcPs_GetInterruptStatus(XTtcPs *t){return t->status;}
static void XTtcPs_ClearInterruptStatus(XTtcPs *t,u32 s){t->status&=~s;}
static int XScuGic_Connect(XScuGic *g,u32 id,Xil_ExceptionHandler h,void *c){(void)g;(void)id;timer_handler=h;timer_callback=c;return 0;}
static void XScuGic_SetPriorityTriggerType(XScuGic *g,u32 id,u8 p,u8 t){(void)g;(void)id;(void)p;(void)t;}
static void XScuGic_Enable(XScuGic *g,u32 id){(void)g;(void)id;}
static XGpioPs_Config *XGpioPs_LookupConfig(u16 id){static XGpioPs_Config c={0};(void)id;return &c;}
static int XGpioPs_CfgInitialize(XGpioPs *g,XGpioPs_Config *c,uintptr_t a){(void)g;(void)c;(void)a;return 0;}
static XUartPs_Config *XUartPs_LookupConfig(u16 id){static XUartPs_Config c={0};(void)id;return &c;}
static int XUartPs_CfgInitialize(XUartPs *u,XUartPs_Config *c,uintptr_t a){(void)u;(void)c;(void)a;return 0;}
static int XUartPs_SetDataFormat(XUartPs *u,XUartPsFormat *f){(void)u;(void)f;return 0;}
static int XUartPs_SetBaudRate(XUartPs *u,u32 b){(void)u;(void)b;return 0;}
static u16 mock_crc16(const u8 *d,u16 n) {
    u16 crc=0xFFFF;for(u16 i=0;i<n;i++){crc^=d[i];for(int b=0;b<8;b++)crc=crc&1?(crc>>1)^0xA001:crc>>1;}return crc;
}
static u32 XUartPs_Send(XUartPs *u,u8 *f,u32 n) {
    (void)u; assert(mock_crc16(f,n-2)==(u16)(f[n-2]|f[n-1]<<8));
    u16 addr=(u16)(f[2]<<8|f[3]), count=(u16)(f[4]<<8|f[5]);
    response_pos=0;response_length=0;
    if(f[1]==3){
        uart_response[0]=f[0];uart_response[1]=3;uart_response[2]=(u8)(2*count);
        for(u16 i=0;i<count;i++){uart_response[3+2*i]=(u8)(screen[addr+i]>>8);uart_response[4+2*i]=(u8)screen[addr+i];}
        response_length=3+2*count;
    } else {
        if(f[1]==6){
            screen[addr]=count;
            if(addr==141 && count==0){clear_writes++;if(drop_clear_echo){drop_clear_echo--;return n;}}
        } else if(f[1]==16){
            for(u16 i=0;i<count;i++)screen[addr+i]=(u16)(f[7+2*i]<<8|f[8+2*i]);
        } else {assert(0);}
        memcpy(uart_response,f,6);response_length=6;
    }
    u16 crc=mock_crc16(uart_response,(u16)response_length);
    uart_response[response_length++]=(u8)crc;uart_response[response_length++]=(u8)(crc>>8);
    return n;
}
static u32 XUartPs_Recv(XUartPs *u,u8 *out,u32 n){
    (void)u;u32 count=response_length-response_pos;if(count>n)count=n;
    memcpy(out,uart_response+response_pos,count);response_pos+=count;return count;
}
static int XUartPs_IsSending(XUartPs *u){(void)u;return 0;}
static XAxiDma_Config *XAxiDma_LookupConfig(u16 id){static XAxiDma_Config c;(void)id;return &c;}
static int XAxiDma_CfgInitialize(XAxiDma *d,XAxiDma_Config *c){(void)d;(void)c;return 0;}
static int XAxiDma_HasSg(XAxiDma *d){(void)d;return 0;}
static void XAxiDma_Reset(XAxiDma *d){(void)d;}
static int XAxiDma_ResetIsDone(XAxiDma *d){(void)d;return 1;}
static void XAxiDma_IntrDisable(XAxiDma *d,u32 m,int dir){(void)d;(void)m;(void)dir;}
static void XAxiDma_IntrEnable(XAxiDma *d,u32 m,int dir){(void)d;(void)m;(void)dir;}
static u32 XAxiDma_IntrGetIrq(XAxiDma *d,int dir){(void)d;(void)dir;return XAXIDMA_IRQ_IOC_MASK;}
static void XAxiDma_IntrAckIrq(XAxiDma *d,u32 m,int dir){(void)d;(void)m;(void)dir;}
static int XAxiDma_SimpleTransfer(XAxiDma *d,UINTPTR a,size_t n,int dir){(void)d;(void)a;(void)n;(void)dir;return 0;}
static void Xil_DCacheFlushRange(UINTPTR a,size_t n){(void)a;(void)n;}
static void Xil_DCacheInvalidateRange(UINTPTR a,size_t n){(void)a;(void)n;}
#endif
'''

CONTROL_GLOBALS = r'''
static XScuGic interrupt_controller;
typedef struct { float fit_k; float fit_b; int collecting; } MockCalibrationState;
static MockCalibrationState calibration_state = {0.8f, 0.0f, 0};
static uint16_t gain_stage=GAIN_INITIAL_MODE;
static int gain_auto_enabled, gain_apply_clear_pending, gain_auto_clear_pending;
static int gain_status_dirty;
static uint16_t gain_auto_requested;
static uint32_t gain_low_samples;
static uint64_t gain_changed_ms, gain_last_capture_ticks;
static uint64_t gain_debug_last_ms;
static uint64_t get_time_ms(void){return mock_now/1000;}
'''

TESTS = r'''
static void rx_edge(u32 v,XTime at) {
    mock_now=at; pins[SIM_CAN_RX_MIO]=v;
    can_gpio_interrupt(0,1U<<SIM_CAN_RX_MIO);
}
static void feed_frame(u8 cmd,u16 value,int bad_crc,int late) {
    u8 f[6]={0xA5,0x20,cmd,(u8)value,(u8)(value>>8),0};
    f[5]=sim_can_crc8(f,5);if(bad_crc)f[5]^=1;
    XTime start=mock_now+30000;rx_edge(1,start-25000);rx_edge(0,start);rx_edge(1,start+50);
    assert(rx_state==2 && rx_timer.interval==100);
    u32 previous=1;
    for(u32 bit=0;bit<48;bit++){
        u32 v=(f[bit/8]>>(bit%8))&1;
        if(v!=previous)rx_edge(v,start+125+bit*50);
        previous=v;pins[SIM_CAN_RX_MIO]=v;
        mock_now=rx_next_sample+(late&&bit==5?25:0);
        rx_timer.status=XTTCPS_IXR_INTERVAL_MASK;
        timer_handler(timer_callback);
        if(late&&bit==5)break;
        if(bit<47)assert(rx_timer.interval==50);
    }
    rx_edge(1,start+2525);
}
static void samples_fill(int16_t *data,int amplitude,int channel_only) {
    for(int s=0;s<DMA_SAMPLES_PER_CH;s++)for(int c=0;c<NUM_CHANNELS;c++)
        data[s*NUM_CHANNELS+c]= (channel_only<0||c==channel_only)?(s&1?amplitude:-amplitude):0;
}
static void observe(int16_t *data,int stable) {
    mock_now+=40960;
    gain_observe_block(data,gain_stage,stable,mock_now);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    SimCanTemperature temp;int16_t data[DMA_BLOCK_SAMPLES];
    pins[10]=1;assert(rs485_modbus_init()==0);
    assert(ad7606_dma_init(&interrupt_controller)==0);assert(gain_init()==0);
    assert(gain_stage==5 && screen[139]==5 && !gain_auto_enabled);
    assert(screen[141]==0 && screen[144]==0 && screen[143]==1);
    puts("PASS startup: manual gain 5, command clears, initial temperature fault");

    static const u16 expected_gain[10]={1,10,15,30,100,150,225,300,450,900};
    for(u16 m=1;m<=10;m++){
        mock_now+=30000;tx_count=0;assert(can_send_gain_level(m)==0);
        assert(tx_count==52);u8 frame[6]={0};
        for(u32 b=0;b<48;b++)frame[b/8]|=(u8)(tx_values[3+b]<<(b%8));
        assert(frame[0]==0xA5 && frame[1]==0x20 && frame[2]==0xA3);
        assert(frame[3]==m && frame[4]==(u8)~m && frame[5]==sim_can_crc8(frame,5));
        assert(can_gain_total(m)==expected_gain[m-1]);
        assert(tx_times[2]-tx_times[1]>=150 && tx_times[3]-tx_times[2]>=100);
    }
    assert(can_send_gain_level(0)==XST_INVALID_PARAM && can_send_gain_level(11)==XST_INVALID_PARAM);
    puts("PASS all ten gain frames, gain factors, invalid modes, TX preamble");

    feed_frame(0xB1,2500,0,0);can_get_temperature(&temp);
    assert(temp.status==0 && temp.centi_celsius==2500 && temp.reports==1);
    feed_frame(0xB1,(u16)(s16)-525,0,0);can_get_temperature(&temp);
    assert(temp.status==0 && temp.centi_celsius==-525 && temp.reports==2);
    temperature_publish();assert(screen[142]==(u16)(s16)-525 && screen[143]==0);
    feed_frame(0xB2,4095,0,0);can_get_temperature(&temp);
    assert(temp.status==1 && temp.centi_celsius==-525 && temp.reports==3);
    temperature_publish();assert(screen[142]==(u16)(s16)-525 && screen[143]==1);
    puts("PASS temperature GPIO/TTC timing, signed little-endian values, fault frame, screen publish");

    feed_frame(0xB1,3333,1,0);feed_frame(0xC1,3333,0,0);feed_frame(0xB1,3333,0,1);
    can_get_temperature(&temp);assert(temp.reports==3 && temp.rejected_frames==3);
    feed_frame(0xB1,1800,0,0);mock_now+=3000001;can_get_temperature(&temp);
    assert(temp.status==1 && temp.centi_celsius==1800);
    feed_frame(0xB1,1900,0,0);can_get_temperature(&temp);assert(temp.status==0);
    puts("PASS CRC/command/late-IRQ rejection, timeout and recovery");

    screen[140]=10;screen[141]=1;screen[142]=1234;screen[143]=1;
    drop_clear_echo=1;tx_count=0;gain_poll_controls();
    assert(gain_stage==10 && gain_apply_clear_pending && screen[141]==0);
    assert(screen[140]==10 && screen[142]==1234 && screen[143]==1);
    int sent=tx_count;gain_poll_controls();
    assert(!gain_apply_clear_pending && tx_count==sent && screen[139]==10);
    screen[140]=11;screen[141]=1;gain_poll_controls();
    assert(screen[141]==0 && gain_stage==10 && tx_count==sent);
    puts("PASS 0142 wire clear, lost-echo retry without repeated gain TX, protected temperature/target registers");

    screen[144]=1;gain_poll_controls();assert(gain_auto_enabled && screen[144]==0);
    screen[140]=4;screen[141]=1;screen[144]=1;gain_poll_controls();
    assert(gain_stage==4 && !gain_auto_enabled && screen[141]==0 && screen[144]==0);
    puts("PASS persistent auto state and manual priority/exit");

    gain_auto_enabled=1;gain_changed_ms=0;gain_low_samples=0;gain_auto_requested=0;
    samples_fill(data,32767,7);observe(data,1);assert(gain_auto_requested<4 && gain_auto_requested>=1);
    gain_auto_service();assert(gain_stage<4);
    gain_stage=4;gain_changed_ms=0;gain_low_samples=0;gain_auto_requested=0;
    samples_fill(data,2500,-1);for(int i=0;i<50;i++)observe(data,1);
    assert(gain_auto_requested==5);gain_auto_service();assert(gain_stage==5);
    gain_stage=1;gain_changed_ms=0;gain_low_samples=0;gain_auto_requested=0;
    samples_fill(data,2000,-1);for(int i=0;i<55;i++)observe(data,1);
    assert(gain_auto_requested==0); /* 1 -> 10 times gain predicts excess RMS */
    gain_stage=10;gain_changed_ms=0;gain_auto_requested=0;gain_low_samples=0;
    samples_fill(data,1,-1);for(int i=0;i<55;i++)observe(data,1);assert(gain_auto_requested==0);
    gain_stage=5;gain_auto_requested=0;gain_low_samples=10000;observe(data,0);assert(gain_low_samples==0);
    gain_stage=5;gain_changed_ms=0;gain_low_samples=0;gain_last_capture_ticks=0;
    samples_fill(data,100,-1);XTime backlog_start=mock_now;
    for(int i=0;i<50;i++){
        XTime captured=backlog_start+(i+1)*40960;
        mock_now=captured+500000;
        gain_observe_block(data,5,1,captured);
        assert(gain_auto_requested==0);
    }
    assert(gain_low_samples>=GAIN_AUTO_RAISE_SAMPLES);
    mock_now=backlog_start+51*40960;gain_observe_block(data,5,1,mock_now);
    assert(gain_auto_requested==6);
    puts("PASS eight-channel overload reduction, sustained-low raise, nonlinear prediction, limits and settling mask");

    reset_state();
    for(int i=0;i<DMA_BLOCK_SAMPLES;i++)data[i]=1000;
    decimate_block(data,5,1);
    float reference=decimated_ring[decimated_write_index-1][0];
    reset_state();for(int i=0;i<DMA_BLOCK_SAMPLES;i++)data[i]=10;
    decimate_block(data,1,1);
    assert(fabsf(decimated_ring[decimated_write_index-1][0]-reference)<0.01f);
    for(int i=0;i<DMA_BLOCK_SAMPLES;i++)data[i]=32767;
    decimate_block(data,1,0);
    assert(decimated_ring[decimated_write_index-1][0]<1001 && data[0]==32767);
    reset_state();capture_gain_mode=5;gain_settle_until=0;
    assert(ad7606_dma_start()==0 && block_gain[0]==5 && block_stable[0]);
    ad7606_dma_gain_begin_change();assert(!block_stable[0]);
    dma_interrupt_handler(&dma);assert(active_index==1 && !block_stable[1]);
    ad7606_dma_set_gain(10);assert(!block_stable[1]);
    dma_interrupt_handler(&dma);assert(block_gain[2]==10 && !block_stable[2]);
    mock_now+=101000;dma_interrupt_handler(&dma);
    assert(block_gain[3]==10 && block_stable[3]);
    assert(block_gain[0]==5 && block_gain[1]==5); /* old blocks retain original tag */
    puts("PASS DMA gain tags across in-flight transfers, settle mask, normalized flow input and unmodified raw samples");
    puts("All firmware behavior checks passed.");return 0;
}
'''


def extract_function(source: str, name: str) -> str:
    match = re.search(r"^static [^\n]+\b" + name + r"\([^;]+?\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError(f"Function not found: {name}")
    start = source.index("{", match.start())
    depth = 1
    for end in range(start + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[match.start():end + 1] + "\n"
    raise RuntimeError(f"Unclosed function: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.gcc:
        parser.error("Native GCC not found; supply --gcc PATH")
    source = (SRC / "main.c").read_text(encoding="utf-8")
    control = "\n".join(extract_function(source, name) for name in (
        "gain_apply_stage", "gain_observe_block", "gain_auto_service",
        "temperature_publish", "gain_init", "gain_poll_controls"))
    with tempfile.TemporaryDirectory(prefix="0806_gain_check_") as directory:
        tmp = Path(directory)
        (tmp / "mock_board.h").write_text(MOCK_HEADER, encoding="utf-8")
        for header in ("xgpiops.h", "xil_types.h", "xscugic.h", "xil_io.h",
                       "xil_printf.h", "xstatus.h", "xtime_l.h", "xttcps.h",
                       "xparameters.h", "xil_exception.h", "xuartps.h",
                       "xaxidma.h", "xil_cache.h", "ad7606_sample.h"):
            (tmp / header).write_text('#include "mock_board.h"\n', encoding="utf-8")
        harness = '#include "can.c"\n#include "rs485_modbus.c"\n#include "ad7606_dma.c"\n'
        harness += CONTROL_GLOBALS + control + TESTS
        (tmp / "verify.c").write_text(harness, encoding="utf-8")
        executable = tmp / "verify.exe"
        subprocess.run([args.gcc, "-std=c11", "-O2", "-Wall", "-Wextra",
                        "-Wno-unused-function", "-I", str(tmp), "-I", str(SRC),
                        str(tmp / "verify.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
