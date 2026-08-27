/**
 * @file usbd_ftdi.c
 * @brief FT2232D emulation: custom TinyUSB class driver + FTDI vendor
 *        control requests.
 *
 * Port of RV-Debugger-BL702/firmware/app/usb2uartjtag/usbd_ftdi.c and the
 * usb_dc_ftdi_send_from_ringbuffer()/usb_dc_ftdi_receive_to_ringbuffer()
 * helpers from main.c (Copyright (c) 2021 Sipeed team, Apache-2.0).
 *
 * The BL702 firmware talks to the USB peripheral registers directly. On the
 * RP2040 we register an application class driver with TinyUSB so we keep the
 * exact same wire behaviour:
 *
 *   - two vendor-specific interfaces, bulk EP 0x81/0x02 and 0x83/0x04
 *   - every IN packet starts with the two modem-status bytes {0x01, 0x60}
 *   - bare status packets are emitted when idle (latency timer)
 *   - all FTDI vendor requests are answered from the emulated EEPROM
 */
#include "usbd_ftdi.h"

#include <string.h>

#include "device/usbd_pvt.h"
#include "mpsse_jtag.h"
#include "pico/time.h"
#include "ring_buffer.h"
#include "tusb.h"
#include "uart_bridge.h"

#define RHPORT 0

/* Upstream polls the JTAG IN endpoint with a bare status packet every 1ms
 * (main.c: `if (mtimer_get_time_us() - last_send > 1000)`). */
#define JTAG_STATUS_POLL_MS 1

extern const uint16_t ftdi_eeprom_info[64];
extern void led_toggle(uint8_t idx);

/* ------------------------------------------------------------------ */
/* FTDI vendor requests                                                 */
/* ------------------------------------------------------------------ */
#define SIO_RESET_REQUEST             0x00 /* Reset the port */
#define SIO_SET_MODEM_CTRL_REQUEST    0x01 /* Set the modem control register */
#define SIO_SET_FLOW_CTRL_REQUEST     0x02 /* Set flow control register */
#define SIO_SET_BAUDRATE_REQUEST      0x03 /* Set baud rate */
#define SIO_SET_DATA_REQUEST          0x04 /* Set the data characteristics */
#define SIO_POLL_MODEM_STATUS_REQUEST 0x05
#define SIO_SET_EVENT_CHAR_REQUEST    0x06
#define SIO_SET_ERROR_CHAR_REQUEST    0x07
#define SIO_SET_LATENCY_TIMER_REQUEST 0x09
#define SIO_GET_LATENCY_TIMER_REQUEST 0x0A
#define SIO_SET_BITMODE_REQUEST       0x0B
#define SIO_READ_PINS_REQUEST         0x0C
#define SIO_READ_EEPROM_REQUEST       0x90
#define SIO_WRITE_EEPROM_REQUEST      0x91
#define SIO_ERASE_EEPROM_REQUEST      0x92

#define SIO_RESET_SIO       0
#define SIO_RESET_PURGE_RX  1
#define SIO_RESET_PURGE_TX  2

#define SIO_SET_DTR_MASK 0x1
#define SIO_SET_DTR_HIGH (1 | (SIO_SET_DTR_MASK << 8))
#define SIO_SET_DTR_LOW  (0 | (SIO_SET_DTR_MASK << 8))
#define SIO_SET_RTS_MASK 0x2
#define SIO_SET_RTS_HIGH (2 | (SIO_SET_RTS_MASK << 8))
#define SIO_SET_RTS_LOW  (0 | (SIO_SET_RTS_MASK << 8))

static uint8_t latency_timer1 = 0x04; /* port A (JTAG) */
static uint8_t latency_timer2 = 0x04; /* port B (UART) */

/* Endpoint packet buffers (must survive the transfer). */
CFG_TUSB_MEM_ALIGN static uint8_t jtag_in_buf[FTDI_EP_SIZE];
CFG_TUSB_MEM_ALIGN static uint8_t uart_in_buf[FTDI_EP_SIZE];
CFG_TUSB_MEM_ALIGN static uint8_t uart_out_buf[FTDI_EP_SIZE];

static uint64_t jtag_last_send_us = 0;
static uint64_t uart_last_send_us = 0;

/* Set when an OUT packet could not be consumed yet (ring buffer full). */
static volatile bool uart_out_stalled = false;

uint32_t usbd_ftdi_get_latency_timer1(void) { return latency_timer1; }
uint32_t usbd_ftdi_get_latency_timer2(void) { return latency_timer2; }

/* ------------------------------------------------------------------ */
/* Baud rate divisor decoding (verbatim from upstream)                  */
/* ------------------------------------------------------------------ */
static void ftdi_set_baudrate(uint32_t itdf_divisor, uint32_t *actual_baudrate)
{
#define FTDI_USB_CLK 48000000
    int baudrate;
    const uint8_t frac[] = { 0, 8, 4, 2, 6, 10, 12, 14 };
    int divisor = itdf_divisor & 0x3fff;
    divisor <<= 4;
    divisor |= frac[(itdf_divisor >> 14) & 0x07];

    if (itdf_divisor == 0x01) {
        baudrate = 2000000;
    } else if (itdf_divisor == 0x00) {
        baudrate = 3000000;
    } else {
        baudrate = FTDI_USB_CLK / divisor;
    }

    /* Kept verbatim from upstream (the condition can never be true, but it
     * is left in place so the port stays a 1:1 translation). */
    if (baudrate > 100000 && baudrate < 12000) {
        *actual_baudrate = (baudrate - 100000) * 100000;
    } else {
        *actual_baudrate = baudrate;
    }
}

/* ------------------------------------------------------------------ */
/* Control transfers                                                    */
/* ------------------------------------------------------------------ */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    static uint32_t actual_baudrate = 1200;

    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    const uint8_t windex_l = (uint8_t)(request->wIndex & 0xff);

    switch (request->bRequest) {
        case SIO_READ_EEPROM_REQUEST: {
            uint8_t idx = windex_l & 0x3f;
            return tud_control_xfer(rhport, request, (void *)&ftdi_eeprom_info[idx], 2);
        }

        case SIO_RESET_REQUEST:
#if FTDI_IMPLEMENT_PURGE
            /* Genuine FT2232D behaviour: wValue selects SIO reset / purge RX
             * / purge TX. Upstream leaves this a no-op; purging keeps us in
             * sync when the host aborts a transfer mid-stream. */
            if (request->wValue == SIO_RESET_SIO || request->wValue == SIO_RESET_PURGE_RX ||
                request->wValue == SIO_RESET_PURGE_TX) {
                if (windex_l == 1) { /* port A - JTAG */
                    jtag_ringbuffer_flush();
                } else if (windex_l == 2) { /* port B - UART */
                    uart_bridge_flush();
                }
            }
#endif
            return tud_control_status(rhport, request);

        case SIO_SET_MODEM_CTRL_REQUEST:
            if (request->wValue == SIO_SET_DTR_HIGH) {
                usbd_ftdi_set_dtr(true);
            } else if (request->wValue == SIO_SET_DTR_LOW) {
                usbd_ftdi_set_dtr(false);
            } else if (request->wValue == SIO_SET_RTS_HIGH) {
                usbd_ftdi_set_rts(true);
            } else if (request->wValue == SIO_SET_RTS_LOW) {
                usbd_ftdi_set_rts(false);
            }
            return tud_control_status(rhport, request);

        case SIO_SET_FLOW_CTRL_REQUEST:
            return tud_control_status(rhport, request);

        case SIO_SET_BAUDRATE_REQUEST: {
            uint8_t baudrate_high = (uint8_t)(request->wIndex >> 8);
            ftdi_set_baudrate(request->wValue | ((uint32_t)baudrate_high << 16), &actual_baudrate);
            if (actual_baudrate != 1200) {
                usbd_ftdi_set_line_coding(actual_baudrate, 8, 0, 0);
            }
            return tud_control_status(rhport, request);
        }

        case SIO_SET_DATA_REQUEST:
            /**
             * D0-D7  databits  BITS_7=7, BITS_8=8
             * D8-D10 parity    NONE=0, ODD=1, EVEN=2, MARK=3, SPACE=4
             * D11-D12 stopbits STOP_BIT_1=0, STOP_BIT_15=1, STOP_BIT_2=2
             * D14    break     BREAK_OFF=0, BREAK_ON=1
             */
            if (actual_baudrate != 1200) {
                usbd_ftdi_set_line_coding(actual_baudrate,
                                          (uint8_t)(request->wValue & 0xff),
                                          (uint8_t)(request->wValue >> 8),
                                          (uint8_t)(request->wValue >> 11));
            }
            return tud_control_status(rhport, request);

        case SIO_POLL_MODEM_STATUS_REQUEST:
            /* Two status bytes, same canned answer as upstream: 0x10 0x60. */
            return tud_control_xfer(rhport, request, (void *)&ftdi_eeprom_info[2], 2);

        case SIO_SET_EVENT_CHAR_REQUEST:
        case SIO_SET_ERROR_CHAR_REQUEST:
            return tud_control_status(rhport, request);

        case SIO_SET_LATENCY_TIMER_REQUEST:
            if (windex_l == 1) {
                latency_timer1 = (uint8_t)(request->wValue & 0xff);
            } else {
                latency_timer2 = (uint8_t)(request->wValue & 0xff);
            }
            return tud_control_status(rhport, request);

        case SIO_GET_LATENCY_TIMER_REQUEST: {
            uint8_t *p = (windex_l == 1) ? &latency_timer1 : &latency_timer2;
            return tud_control_xfer(rhport, request, p, 1);
        }

        case SIO_SET_BITMODE_REQUEST:
            /* We are permanently in MPSSE mode on port A, so the requested
             * mode is accepted and ignored (as upstream does). */
            return tud_control_status(rhport, request);

        default:
            return false; /* stall */
    }
}

/* ------------------------------------------------------------------ */
/* Class driver                                                         */
/* ------------------------------------------------------------------ */
static void ftdi_driver_init(void)
{
    latency_timer1 = 0x04;
    latency_timer2 = 0x04;
    uart_out_stalled = false;
}

static bool ftdi_driver_deinit(void)
{
    return true;
}

static void ftdi_driver_reset(uint8_t rhport)
{
    (void)rhport;
    /* usbd_ftdi_reset() in upstream */
    latency_timer1 = 0x10;
    latency_timer2 = 0x10;
    uart_out_stalled = false;
    jtag_last_send_us = 0;
    uart_last_send_us = 0;
}

static uint16_t ftdi_driver_open(uint8_t rhport, tusb_desc_interface_t const *desc_intf, uint16_t max_len)
{
    /* Both interfaces are vendor specific with two bulk endpoints. */
    TU_VERIFY(desc_intf->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC, 0);

    const uint16_t drv_len = (uint16_t)(sizeof(tusb_desc_interface_t) + 2 * sizeof(tusb_desc_endpoint_t));
    TU_VERIFY(max_len >= drv_len, 0);

    uint8_t ep_out = 0, ep_in = 0;
    TU_ASSERT(usbd_open_edpt_pair(rhport, (uint8_t const *)desc_intf + sizeof(tusb_desc_interface_t), 2,
                                  TUSB_XFER_BULK, &ep_out, &ep_in),
              0);

    if (desc_intf->bInterfaceNumber == 0) {
        TU_ASSERT(ep_out == JTAG_OUT_EP && ep_in == JTAG_IN_EP, 0);
        /* Arm the MPSSE OUT endpoint. */
        TU_ASSERT(usbd_edpt_xfer(rhport, JTAG_OUT_EP, jtag_rx_buffer_get(), FTDI_EP_SIZE), 0);
    } else {
        TU_ASSERT(ep_out == CDC_OUT_EP && ep_in == CDC_IN_EP, 0);
        TU_ASSERT(usbd_edpt_xfer(rhport, CDC_OUT_EP, uart_out_buf, FTDI_EP_SIZE), 0);
    }

    return drv_len;
}

static bool ftdi_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)result;

    switch (ep_addr) {
        case JTAG_OUT_EP:
            /* Hand the packet to the MPSSE state machine. The endpoint stays
             * un-armed until every byte has been consumed - this is the flow
             * control upstream implements with `jtag_received_flag`. */
            if (xferred_bytes) {
                jtag_rx_submit(xferred_bytes);
            } else {
                usbd_edpt_xfer(rhport, JTAG_OUT_EP, jtag_rx_buffer_get(), FTDI_EP_SIZE);
            }
            break;

        case CDC_OUT_EP:
            if (xferred_bytes) {
                rb_write(&usb_rx_rb, uart_out_buf, xferred_bytes);
            }
            /* Only re-arm while there is room for a full packet, otherwise
             * NAK the host until the UART has drained (upstream's
             * `overflow_flag`). */
            if (rb_free(&usb_rx_rb) >= FTDI_EP_SIZE) {
                usbd_edpt_xfer(rhport, CDC_OUT_EP, uart_out_buf, FTDI_EP_SIZE);
            } else {
                uart_out_stalled = true;
            }
            break;

        case JTAG_IN_EP:
        case CDC_IN_EP:
        default:
            break; /* next packet is scheduled from usbd_ftdi_task() */
    }

    return true;
}

static usbd_class_driver_t const ftdi_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "FTDI",
#endif
    .init            = ftdi_driver_init,
    .deinit          = ftdi_driver_deinit,
    .reset           = ftdi_driver_reset,
    .open            = ftdi_driver_open,
    .control_xfer_cb = NULL, /* vendor requests arrive via tud_vendor_control_xfer_cb */
    .xfer_cb         = ftdi_driver_xfer_cb,
    .sof             = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &ftdi_driver;
}

/* ------------------------------------------------------------------ */
/* IN endpoint pump                                                     */
/* ------------------------------------------------------------------ */
/**
 * Emits one IN packet: two status bytes followed by up to 62 payload bytes.
 * When there is no payload a bare status packet is still emitted once the
 * latency timer expires - this is what makes libftdi's blocking reads
 * return instead of timing out.
 */
static void ftdi_in_pump(uint8_t ep, ring_buffer_t *rb, uint8_t *buf, uint64_t *last_send_us,
                         uint32_t latency_ms, bool blink)
{
    if (usbd_edpt_busy(RHPORT, ep) || usbd_edpt_stalled(RHPORT, ep)) {
        return;
    }
    if (!usbd_edpt_claim(RHPORT, ep)) {
        return;
    }

    uint32_t n = rb_read(rb, buf + 2, FTDI_EP_SIZE - 2);
    uint64_t now = time_us_64();

    if (n == 0) {
        if (latency_ms == 0) {
            latency_ms = 1;
        }
        if ((now - *last_send_us) < (uint64_t)latency_ms * 1000u) {
            usbd_edpt_release(RHPORT, ep);
            return;
        }
    }

    buf[0] = FTDI_STATUS_BYTE0;
    buf[1] = FTDI_STATUS_BYTE1;

    if (usbd_edpt_xfer(RHPORT, ep, buf, (uint16_t)(n + 2))) {
        *last_send_us = now;
        if (n && blink) {
            led_toggle(0); /* RX indication */
        }
    } else {
        usbd_edpt_release(RHPORT, ep);
    }
}

void usbd_ftdi_init(void)
{
    jtag_last_send_us = 0;
    uart_last_send_us = 0;
}

void usbd_ftdi_task(void)
{
    if (!tud_mounted()) {
        return;
    }

    /* MPSSE responses (port A). */
    ftdi_in_pump(JTAG_IN_EP, &jtag_tx_rb, jtag_in_buf, &jtag_last_send_us, JTAG_STATUS_POLL_MS, false);

    /* UART receive data (port B). */
    ftdi_in_pump(CDC_IN_EP, &uart1_rx_rb, uart_in_buf, &uart_last_send_us, latency_timer2, true);

    /* Re-arm the MPSSE OUT endpoint once the state machine is idle. */
    if (!jtag_rx_pending() && !usbd_edpt_busy(RHPORT, JTAG_OUT_EP)) {
        if (usbd_edpt_claim(RHPORT, JTAG_OUT_EP)) {
            if (!usbd_edpt_xfer(RHPORT, JTAG_OUT_EP, jtag_rx_buffer_get(), FTDI_EP_SIZE)) {
                usbd_edpt_release(RHPORT, JTAG_OUT_EP);
            }
        }
    }

    /* Re-arm the UART OUT endpoint once the TX ring buffer has drained. */
    if (uart_out_stalled && rb_free(&usb_rx_rb) >= FTDI_EP_SIZE) {
        if (usbd_edpt_claim(RHPORT, CDC_OUT_EP)) {
            if (usbd_edpt_xfer(RHPORT, CDC_OUT_EP, uart_out_buf, FTDI_EP_SIZE)) {
                uart_out_stalled = false;
            } else {
                usbd_edpt_release(RHPORT, CDC_OUT_EP);
            }
        }
    }
}
