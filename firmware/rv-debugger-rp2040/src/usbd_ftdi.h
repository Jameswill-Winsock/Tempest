/**
 * @file usbd_ftdi.h
 * @brief FT2232D device emulation on TinyUSB.
 *
 * Port of RV-Debugger-BL702/firmware/app/usb2uartjtag/usbd_ftdi.{c,h}
 * (Copyright (c) 2021 Sipeed team, Apache-2.0).
 */
#ifndef _USBD_FTDI_H
#define _USBD_FTDI_H

#include <stdbool.h>
#include <stdint.h>

/* Endpoint addresses - identical to upstream (and to a real FT2232D). */
#define CDC_IN_EP   0x83 /* port B (UART) device -> host */
#define CDC_OUT_EP  0x04 /* port B (UART) host   -> device */
#define JTAG_IN_EP  0x81 /* port A (MPSSE) device -> host */
#define JTAG_OUT_EP 0x02 /* port A (MPSSE) host   -> device */

#define FTDI_EP_SIZE 64

/* Every IN packet is prefixed with the two FTDI modem-status bytes. */
#define FTDI_STATUS_BYTE0 0x01
#define FTDI_STATUS_BYTE1 0x60

void usb_descriptor_init(void);

void usbd_ftdi_init(void);
/* Pumps the two IN endpoints (data + periodic bare status packets) and
 * re-arms the OUT endpoints. Call from the main loop next to tud_task(). */
void usbd_ftdi_task(void);

uint32_t usbd_ftdi_get_latency_timer1(void);
uint32_t usbd_ftdi_get_latency_timer2(void);

/* Implemented by the application (main.c). */
void usbd_ftdi_set_line_coding(uint32_t baudrate, uint8_t databits, uint8_t parity, uint8_t stopbits);
void usbd_ftdi_set_dtr(bool dtr);
void usbd_ftdi_set_rts(bool rts);

#endif /* _USBD_FTDI_H */
