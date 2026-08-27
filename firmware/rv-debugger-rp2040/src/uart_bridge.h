/**
 * @file uart_bridge.h
 * @brief USB <-> UART bridge (FTDI port B).
 */
#ifndef _UART_BRIDGE_H
#define _UART_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buffer.h"

extern ring_buffer_t usb_rx_rb;   /* USB OUT -> UART TX */
extern ring_buffer_t uart1_rx_rb; /* UART RX -> USB IN  */

void uart_ringbuffer_init(void);
void uart1_init(void);
void uart1_config(uint32_t baudrate, uint8_t databits, uint8_t parity, uint8_t stopbits);

void uart1_dtr_rts_init(void);
void dtr_pin_set(bool status);
void rts_pin_set(bool status);

/* Pushes buffered USB data out of the UART (DMA). Call from the main loop. */
void uart_send_from_ringbuffer(void);
void uart_bridge_flush(void);

#endif /* _UART_BRIDGE_H */
