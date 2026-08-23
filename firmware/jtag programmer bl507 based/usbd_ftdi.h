#ifndef _USBD_FTDI_H
#define _USBD_FTDI_H

#define CDC_IN_EP 	0x83
#define CDC_OUT_EP 	0x04

#define JTAG_IN_EP 0x81
#define JTAG_OUT_EP 0x02

void usbd_ftdi_add_interface(usbd_class_t *class, usbd_interface_t *intf);

void usbd_ftdi_set_line_coding(uint32_t baudrate, uint8_t databits, uint8_t parity, uint8_t stopbits);
void usbd_ftdi_set_dtr(bool dtr);
void usbd_ftdi_set_rts(bool rts);
uint32_t usbd_ftdi_get_sof_tick(void);
uint32_t usbd_ftdi_get_latency_timer1(void);
uint32_t usbd_ftdi_get_latency_timer2(void);
#endif /* USB_FTDI_H_ */
