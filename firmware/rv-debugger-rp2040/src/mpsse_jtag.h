/**
 * @file mpsse_jtag.h
 * @brief MPSSE (FTDI JTAG) command interpreter.
 */
#ifndef _MPSSE_JTAG_H
#define _MPSSE_JTAG_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buffer.h"

/* MPSSE responses waiting to be shipped to the host on EP 0x81. */
extern ring_buffer_t jtag_tx_rb;

void jtag_gpio_init(void);
void jtag_ringbuffer_init(void);
void jtag_ringbuffer_flush(void);

/* USB plumbing: the OUT endpoint transfers straight into this buffer. */
uint8_t *jtag_rx_buffer_get(void);
void jtag_rx_submit(uint32_t len);
bool jtag_rx_pending(void);

/* Runs the state machine over the pending OUT packet. */
void jtag_process(void);

#endif /* _MPSSE_JTAG_H */
