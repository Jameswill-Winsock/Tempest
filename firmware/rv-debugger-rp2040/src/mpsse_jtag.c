/**
 * @file mpsse_jtag.c
 * @brief MPSSE command interpreter, ported 1:1 from
 *        RV-Debugger-BL702/firmware/app/usb2uartjtag/jtag_process.c
 *        (Copyright (c) 2021 Sipeed team, Apache-2.0).
 *
 * The state machine, the opcode decoding, the bit order of every shift and
 * the "which commands echo data back" rules are deliberately identical to
 * the BL702 firmware: hosts such as the Gowin programmer and OpenOCD rely on
 * this exact behaviour.
 *
 * Differences forced by the target:
 *   - BL702 register writes (0x40000188 / 0x40000180) become RP2040 SIO
 *     GPIO set/clr/in accesses.
 *   - The BL702 build placed the hot code/data in TCM; here the equivalent
 *     is __not_in_flash_func() so TCK timing does not depend on XIP cache
 *     hits.
 *   - Upstream advances the state machine one byte per main-loop pass; we
 *     drain the whole 64 byte packet in one call, which is equivalent (a
 *     64 byte command block can emit at most 64 response bytes, and the
 *     response ring buffer holds 1 KiB).
 */
#include "mpsse_jtag.h"

#include <string.h>

#ifndef JTAG_SIM
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "io_cfg.h"
#include "pico/stdlib.h"
#endif

#include "jtag_pins.h"

#ifdef JTAG_SIM
#define __not_in_flash_func(f) f
#undef __always_inline
#define __always_inline inline
#endif

/**
 * Half-period padding. The BL702 bit-bangs at roughly 5 MHz; a bare RP2040
 * loop at 125 MHz would run noticeably faster, so each TCK level is padded
 * with JTAG_TCK_DELAY nops. See README for measured values / tuning.
 */
#ifndef JTAG_TCK_DELAY
#define JTAG_TCK_DELAY 3
#endif

static __always_inline void jtag_delay(void)
{
#if JTAG_TCK_DELAY > 0
    for (uint32_t i = 0; i < (uint32_t)JTAG_TCK_DELAY; i++) {
        __asm volatile("nop" ::: "memory");
    }
#endif
}

/* ------------------------------------------------------------------ */
/* MPSSE state machine states (upstream numbering)                      */
/* ------------------------------------------------------------------ */
#define MPSSE_IDLE              0
#define MPSSE_RCV_LENGTH_L      1
#define MPSSE_RCV_LENGTH_H      2
#define MPSSE_TRANSMIT_BYTE     3
#define MPSSE_RCV_LENGTH        4
#define MPSSE_TRANSMIT_BIT      5
#define MPSSE_ERROR             6
#define MPSSE_TRANSMIT_BIT_MSB  7
#define MPSSE_TMS_OUT           8
#define MPSSE_NO_OP_1           9
#define MPSSE_NO_OP_2           10
#define MPSSE_TRANSMIT_BYTE_MSB 11
#define MPSSE_RUN_TEST          12

#define JTAG_TX_BUFFER_SIZE (1 * 1024)
#define JTAG_RX_BUFFER_SIZE (64)

static uint8_t jtag_tx_buffer[JTAG_TX_BUFFER_SIZE];
ring_buffer_t jtag_tx_rb;

static uint8_t jtag_rx_buffer[JTAG_RX_BUFFER_SIZE];
static volatile uint32_t jtag_rx_len = 0;
static volatile uint32_t jtag_rx_pos = 0;
static volatile bool jtag_received_flag = false;

static uint32_t mpsse_longlen = 0;
static uint32_t mpsse_shortlen = 0;
static uint32_t mpsse_status = MPSSE_IDLE;
static uint32_t jtag_cmd = 0;

static inline void jtag_write(uint8_t data)
{
    rb_write_byte(&jtag_tx_rb, data);
}

uint8_t *jtag_rx_buffer_get(void)
{
    return jtag_rx_buffer;
}

void jtag_rx_submit(uint32_t len)
{
    jtag_rx_len = len;
    jtag_rx_pos = 0;
    jtag_received_flag = true;
}

bool jtag_rx_pending(void)
{
    return jtag_received_flag;
}

void jtag_ringbuffer_init(void)
{
    memset(jtag_tx_buffer, 0, JTAG_TX_BUFFER_SIZE);
    rb_init(&jtag_tx_rb, jtag_tx_buffer, JTAG_TX_BUFFER_SIZE);
}

void jtag_ringbuffer_flush(void)
{
    jtag_tx_rb.tail = jtag_tx_rb.head;
}

/* ------------------------------------------------------------------ */
/* Gowin internal-flash quirk (jtag_process.c.gowin)                    */
/* ------------------------------------------------------------------ */
/*
 * Some Gowin parts (the internal-flash GW1N/GW1NZ family) need a genuinely
 * free running TCK during the long "run test idle" phase the programmer
 * emits as a multi-thousand byte clock-only block. Upstream handles it by
 * handing TCK to a PWM channel for the duration. Enable with
 * -DGOWIN_INT_FLASH_QUIRK=1.
 */
#ifndef GOWIN_INT_FLASH_QUIRK
#define GOWIN_INT_FLASH_QUIRK 0
#endif

#if GOWIN_INT_FLASH_QUIRK && !defined(JTAG_SIM)
#ifndef GOWIN_RUNTEST_TCK_HZ
#define GOWIN_RUNTEST_TCK_HZ 2500000u /* BL702: bclk/29, ~2.5 MHz */
#endif

static uint32_t pwm_slice;

static void pwm_start(void)
{
    gpio_set_function(TCK_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(pwm_slice, true);
}

static void pwm_stop(void)
{
    pwm_set_enabled(pwm_slice, false);
    gpio_set_function(TCK_PIN, GPIO_FUNC_SIO);
    TCK_LOW();
}

static void jtag_pwm_init(void)
{
    pwm_slice = pwm_gpio_to_slice_num(TCK_PIN);
    uint32_t wrap = clock_get_hz(clk_sys) / GOWIN_RUNTEST_TCK_HZ;
    if (wrap < 2) {
        wrap = 2;
    }
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, (uint16_t)(wrap - 1));
    pwm_init(pwm_slice, &cfg, false);
    pwm_set_chan_level(pwm_slice, pwm_gpio_to_channel(TCK_PIN), (uint16_t)(wrap / 2));
}
#endif /* GOWIN_INT_FLASH_QUIRK */

void jtag_gpio_init(void)
{
#ifndef JTAG_SIM
    gpio_init(TMS_PIN);
    gpio_init(TDI_PIN);
    gpio_init(TCK_PIN);
    gpio_init(TDO_PIN);

    gpio_set_dir(TMS_PIN, GPIO_OUT);
    gpio_set_dir(TDI_PIN, GPIO_OUT);
    gpio_set_dir(TCK_PIN, GPIO_OUT);
    gpio_set_dir(TDO_PIN, GPIO_IN);

    /* Fast slew + max drive: the debugger usually sits behind a flying lead. */
    gpio_set_slew_rate(TCK_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(TDI_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(TMS_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(TCK_PIN, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(TDI_PIN, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(TMS_PIN, GPIO_DRIVE_STRENGTH_8MA);

    TMS_LOW();
    TDI_LOW();
    TCK_LOW();

#if GOWIN_INT_FLASH_QUIRK
    jtag_pwm_init();
#endif
#else
    TMS_LOW();
    TDI_LOW();
    TCK_LOW();
#endif /* JTAG_SIM */
}

/* ------------------------------------------------------------------ */
/* State machine                                                        */
/* ------------------------------------------------------------------ */
static void __not_in_flash_func(jtag_step)(void)
{
    uint32_t usb_tx_data = 0;
    uint32_t data = 0;

    switch (mpsse_status) {
        case MPSSE_IDLE:
            jtag_cmd = jtag_rx_buffer[jtag_rx_pos];

            switch (jtag_cmd) {
                case 0x80:
                case 0x82: /* set data bits low/high byte - pseudo bit-bang */
                    mpsse_status = MPSSE_NO_OP_1;
                    jtag_rx_pos++;
                    break;

                case 0x81:
                case 0x83: /* read data bits low/high byte - canned answer */
                    usb_tx_data = jtag_rx_buffer[jtag_rx_pos] - 0x80;
                    jtag_write(usb_tx_data);
                    jtag_rx_pos++;
                    break;

                case 0x84:
                case 0x85: /* loopback on/off */
                    jtag_rx_pos++;
                    break;

                case 0x86: /* set clock divisor - not supported, swallowed */
                    mpsse_status = MPSSE_NO_OP_1;
                    jtag_rx_pos++;
                    break;

                case 0x87: /* send immediate */
                    jtag_rx_pos++;
                    break;

                /* byte-length data shifts */
                case 0x19:
                case 0x1d:
                case 0x39:
                case 0x3d:
                case 0x11:
                case 0x15:
                case 0x31:
                case 0x35:
                    mpsse_status = MPSSE_RCV_LENGTH_L;
                    jtag_rx_pos++;
                    break;

                /* bit-length data shifts and TMS shifts */
                case 0x6b:
                case 0x6f:
                case 0x4b:
                case 0x4f:
                case 0x3b:
                case 0x3f:
                case 0x1b:
                case 0x1f:
                case 0x13:
                case 0x17:
                    mpsse_status = MPSSE_RCV_LENGTH;
                    jtag_rx_pos++;
                    break;

                default:
                    /* Bad command: FTDI answers 0xFA followed by the offending
                     * byte. */
                    usb_tx_data = 0xFA;
                    jtag_write(usb_tx_data);
                    mpsse_status = MPSSE_ERROR;
                    break;
            }
            break;

        case MPSSE_RCV_LENGTH_L:
            mpsse_longlen = jtag_rx_buffer[jtag_rx_pos];
            mpsse_status = MPSSE_RCV_LENGTH_H;
            jtag_rx_pos++;
            break;

        case MPSSE_RCV_LENGTH_H:
            mpsse_longlen |= (jtag_rx_buffer[jtag_rx_pos] << 8) & 0xff00;
            jtag_rx_pos++;
#if GOWIN_INT_FLASH_QUIRK
            if ((mpsse_longlen >= 8000) && (jtag_cmd & (1 << 5)) == 0) {
                pwm_start();
                mpsse_status = MPSSE_RUN_TEST;
            } else if (jtag_cmd == 0x11 || jtag_cmd == 0x31)
#else
            if (jtag_cmd == 0x11 || jtag_cmd == 0x31 || jtag_cmd == 0x15 || jtag_cmd == 0x35)
#endif
            {
                mpsse_status = MPSSE_TRANSMIT_BYTE_MSB;
            } else {
                mpsse_status = MPSSE_TRANSMIT_BYTE;
            }
            break;

        case MPSSE_TRANSMIT_BYTE: /* LSB first */
            data = jtag_rx_buffer[jtag_rx_pos];
            usb_tx_data = 0;

            for (uint32_t i = 8; i; i--) {
                TCK_LOW();
                if (data & 0x01) {
                    TDI_HIGH();
                } else {
                    TDI_LOW();
                }
                data >>= 1;
                usb_tx_data >>= 1;
                jtag_delay();

                TCK_HIGH();
                if (TDO_READ()) {
                    usb_tx_data |= 0x80;
                }
                jtag_delay();
            }
            TCK_LOW();

            if (jtag_cmd == 0x39 || jtag_cmd == 0x3d) {
                jtag_write(usb_tx_data);
            }

            if (mpsse_longlen == 0) {
                mpsse_status = MPSSE_IDLE;
            }
            mpsse_longlen--;
            jtag_rx_pos++;
            break;

        case MPSSE_TRANSMIT_BYTE_MSB:
            data = jtag_rx_buffer[jtag_rx_pos];
            usb_tx_data = 0;

            for (uint32_t i = 8; i; i--) {
                TCK_LOW();
                if (data & 0x80) {
                    TDI_HIGH();
                } else {
                    TDI_LOW();
                }
                data <<= 1;
                usb_tx_data <<= 1;
                jtag_delay();

                TCK_HIGH();
                if (TDO_READ()) {
                    usb_tx_data |= 0x01;
                }
                jtag_delay();
            }
            TCK_LOW();

            if (jtag_cmd == 0x31 || jtag_cmd == 0x35) {
                jtag_write(usb_tx_data);
            }

            if (mpsse_longlen == 0) {
                mpsse_status = MPSSE_IDLE;
            }
            jtag_rx_pos++;
            mpsse_longlen--;
            break;

        case MPSSE_RCV_LENGTH:
            mpsse_shortlen = jtag_rx_buffer[jtag_rx_pos];

            if (jtag_cmd == 0x6b || jtag_cmd == 0x4b || jtag_cmd == 0x6f || jtag_cmd == 0x4f) {
                mpsse_status = MPSSE_TMS_OUT;
            } else if (jtag_cmd == 0x13 || jtag_cmd == 0x17) {
                mpsse_status = MPSSE_TRANSMIT_BIT_MSB;
            } else {
                mpsse_status = MPSSE_TRANSMIT_BIT;
            }
            jtag_rx_pos++;
            break;

        case MPSSE_TRANSMIT_BIT: /* LSB first, mpsse_shortlen+1 bits */
            data = jtag_rx_buffer[jtag_rx_pos];
            usb_tx_data = 0;

            do {
                TCK_LOW();
                if (data & 0x01) {
                    TDI_HIGH();
                } else {
                    TDI_LOW();
                }
                data >>= 1;
                usb_tx_data >>= 1;
                jtag_delay();

                TCK_HIGH();
                if (TDO_READ()) {
                    usb_tx_data |= 0x80;
                }
                jtag_delay();
            } while ((mpsse_shortlen--) > 0);
            TCK_LOW();

            if (jtag_cmd == 0x3b || jtag_cmd == 0x3f) {
                jtag_write(usb_tx_data);
            }

            mpsse_status = MPSSE_IDLE;
            jtag_rx_pos++;
            break;

        case MPSSE_TRANSMIT_BIT_MSB:
            data = jtag_rx_buffer[jtag_rx_pos];

            do {
                TCK_LOW();
                if (data & 0x80) {
                    TDI_HIGH();
                } else {
                    TDI_LOW();
                }
                data <<= 1;
                jtag_delay();

                TCK_HIGH();
                jtag_delay();
            } while ((mpsse_shortlen--) > 0);
            TCK_LOW();

            mpsse_status = MPSSE_IDLE;
            jtag_rx_pos++;
            break;

        case MPSSE_ERROR:
            usb_tx_data = jtag_rx_buffer[jtag_rx_pos];
            jtag_write(usb_tx_data);
            mpsse_status = MPSSE_IDLE;
            jtag_rx_pos++;
            break;

        case MPSSE_TMS_OUT: /* clock TMS, TDI held at bit 7 of the data byte */
            data = jtag_rx_buffer[jtag_rx_pos];

            if (data & 0x80) {
                TDI_HIGH();
            } else {
                TDI_LOW();
            }

            usb_tx_data = 0;

            do {
                TCK_LOW();
                if (data & 0x01) {
                    TMS_HIGH();
                } else {
                    TMS_LOW();
                }
                data >>= 1;
                usb_tx_data >>= 1;
                jtag_delay();

                TCK_HIGH();
                if (TDO_READ()) {
                    usb_tx_data |= 0x80;
                }
                jtag_delay();
            } while ((mpsse_shortlen--) > 0);
            TCK_LOW();

            if (jtag_cmd == 0x6b || jtag_cmd == 0x6f) {
                jtag_write(usb_tx_data);
            }

            mpsse_status = MPSSE_IDLE;
            jtag_rx_pos++;
            break;

        case MPSSE_NO_OP_1:
            jtag_rx_pos++;
            mpsse_status = MPSSE_NO_OP_2;
            break;

        case MPSSE_NO_OP_2:
            mpsse_status = MPSSE_IDLE;
            jtag_rx_pos++;
            break;

#if GOWIN_INT_FLASH_QUIRK
        case MPSSE_RUN_TEST:
            if (mpsse_longlen == 0) {
                mpsse_status = MPSSE_IDLE;
                pwm_stop();
            }

            for (uint32_t i = 0; i < 50; i++) {
                __asm volatile("nop");
            }

            jtag_rx_pos++;
            mpsse_longlen--;
            break;
#endif

        default:
            mpsse_status = MPSSE_IDLE;
            break;
    }
}

void __not_in_flash_func(jtag_process)(void)
{
    if (!jtag_received_flag) {
        return;
    }

    while (jtag_rx_pos < jtag_rx_len) {
        jtag_step();
    }

    /* Packet consumed - usbd_ftdi_task() re-arms the OUT endpoint. */
    jtag_received_flag = false;
}
