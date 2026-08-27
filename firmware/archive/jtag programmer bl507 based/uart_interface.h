#ifndef __UART_IF_H__
#define __UART_IF_H__

#include "hal_uart.h"
#include "ring_buffer.h"

extern Ring_Buffer_Type usb_rx_rb;
extern Ring_Buffer_Type uart1_rx_rb;

void uart1_init(void);
void uart1_config(uint32_t baudrate,uart_databits_t databits,uart_parity_t parity,uart_stopbits_t stopbits);
void uart1_set_dtr_rts(uint8_t dtr, uint8_t rts);
void uart1_dtr_init(void);
void uart1_rts_init(void);
void uart1_dtr_deinit(void);
void uart1_rts_deinit(void);
void dtr_pin_set(uint8_t status);
void rts_pin_set(uint8_t status);
void uart_ringbuffer_init(void);
void uart_send_from_ringbuffer(void);
#endif