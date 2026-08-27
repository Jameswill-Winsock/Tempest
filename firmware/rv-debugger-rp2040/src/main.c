/**
 * @file main.c
 * @brief RV-Debugger-RP2040: FT2232D (JTAG + UART) debugger firmware.
 *
 * Port of RV-Debugger-BL702/firmware/app/usb2uartjtag/main.c
 * (Copyright (c) 2021 Sipeed team, Apache-2.0) to the Raspberry Pi RP2040.
 *
 * UART:
 *   RXD -> ringbuffer -> IN  EP 0x83
 *   TXD <- ringbuffer <- OUT EP 0x04
 *
 * JTAG:
 *   OUT EP 0x02 -> jtag_rx_buffer -> MPSSE state machine -> bit-banged
 *   TCK/TMS/TDI/TDO, responses -> ringbuffer -> IN EP 0x81
 */
#include <stdio.h>

#include "io_cfg.h"
#include "mpsse_jtag.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "uart_bridge.h"
#include "usbd_ftdi.h"

/************************  led ctrl functions  ************************/
/* A pin of -1 means "this board has no such LED" (e.g. Shrike Lite has a
 * single MCU-driven user LED; the other one belongs to its FPGA). */
static const int32_t led_pins[2] = { LED0_PIN, LED1_PIN };
static const bool led_active_low[2] = { LED0_ACTIVE_LOW, LED1_ACTIVE_LOW };
static volatile uint8_t led_stat[2] = { 0, 0 };

static void led_gpio_init(void)
{
    for (int i = 0; i < 2; i++) {
        if (led_pins[i] < 0) {
            continue;
        }
        gpio_init((uint)led_pins[i]);
        gpio_set_dir((uint)led_pins[i], GPIO_OUT);
    }
}

void led_set(uint8_t idx, uint8_t status)
{
    led_stat[idx] = status;
    if (led_pins[idx] < 0) {
        return;
    }
    gpio_put((uint)led_pins[idx], led_active_low[idx] ? !status : status);
}

void led_toggle(uint8_t idx)
{
    led_set(idx, !led_stat[idx]);
}

/************************  API for usbd_ftdi  ************************/
void usbd_ftdi_set_line_coding(uint32_t baudrate, uint8_t databits, uint8_t parity, uint8_t stopbits)
{
    uart1_config(baudrate, databits, parity, stopbits);
}

void usbd_ftdi_set_dtr(bool dtr)
{
    dtr_pin_set(!dtr);
}

void usbd_ftdi_set_rts(bool rts)
{
    rts_pin_set(!rts);
}

int main(void)
{
    uart_ringbuffer_init();
    uart1_init();
    uart1_dtr_rts_init();

    led_gpio_init();
    led_set(0, 1); /* led0: RX/TX indication */
    led_set(1, 1); /* led1: power indication */

    jtag_ringbuffer_init();
    jtag_gpio_init();

    usb_descriptor_init();
    usbd_ftdi_init();
    tusb_init();

    while (!tud_mounted()) {
        tud_task();
    }

    led_toggle(0);

    while (1) {
        tud_task();               /* USB stack */
        usbd_ftdi_task();         /* FTDI IN/OUT endpoint pumps */
        uart_send_from_ringbuffer();
        jtag_process();           /* MPSSE state machine */
    }

    return 0;
}
