/**
 * @file uart_bridge.c
 * @brief USB <-> UART bridge, ported from
 *        RV-Debugger-BL702/firmware/app/usb2uartjtag/uart_interface.c
 *        (Copyright (c) 2021 Sipeed team, Apache-2.0).
 *
 * Same structure as upstream: two 8 KiB ring buffers, interrupt driven RX
 * and DMA driven TX, with DTR/RTS exposed as plain GPIOs.
 */
#include "uart_bridge.h"

#include <string.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "io_cfg.h"
#include "pico/stdlib.h"

#define USB_OUT_RINGBUFFER_SIZE (8 * 1024)
#define UART_RX_RINGBUFFER_SIZE (8 * 1024)
#define UART_TX_DMA_SIZE        (4096)

static uint8_t usb_rx_mem[USB_OUT_RINGBUFFER_SIZE];
static uint8_t uart_rx_mem[UART_RX_RINGBUFFER_SIZE];
static uint8_t src_buffer[UART_TX_DMA_SIZE];

ring_buffer_t usb_rx_rb;
ring_buffer_t uart1_rx_rb;

static int uart_tx_dma_chan = -1;

extern void led_toggle(uint8_t idx);

/* ------------------------------------------------------------------ */
/* RX interrupt                                                         */
/* ------------------------------------------------------------------ */
static void uart_rx_irq_handler(void)
{
    while (uart_is_readable(UART_ID)) {
        uint8_t c = (uint8_t)uart_get_hw(UART_ID)->dr;
        rb_write_byte(&uart1_rx_rb, c);
    }
    /* Clear the receive-timeout flag (RX flag self-clears with the FIFO). */
    uart_get_hw(UART_ID)->icr = UART_UARTICR_RTIC_BITS;
}

void uart_ringbuffer_init(void)
{
    memset(usb_rx_mem, 0, sizeof(usb_rx_mem));
    memset(uart_rx_mem, 0, sizeof(uart_rx_mem));

    rb_init(&usb_rx_rb, usb_rx_mem, USB_OUT_RINGBUFFER_SIZE);
    rb_init(&uart1_rx_rb, uart_rx_mem, UART_RX_RINGBUFFER_SIZE);
}

void uart1_init(void)
{
    uart_init(UART_ID, 115200);
    gpio_set_function(UART_TXD_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RXD_PIN, GPIO_FUNC_UART);
    gpio_pull_up(UART_RXD_PIN);

    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    /* RX FIFO 1/2 full triggers an interrupt; the receive timeout catches
     * the tail of short bursts (upstream uses UART_RX_FIFO_IT | UART_RTO_IT). */
    hw_write_masked(&uart_get_hw(UART_ID)->ifls, 2u << UART_UARTIFLS_RXIFLSEL_LSB,
                    UART_UARTIFLS_RXIFLSEL_BITS);

    irq_set_exclusive_handler(UART_IRQ_ID, uart_rx_irq_handler);
    irq_set_enabled(UART_IRQ_ID, true);
    uart_set_irqs_enabled(UART_ID, true, false);

    /* TX DMA */
    uart_tx_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(uart_tx_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, uart_get_dreq(UART_ID, true));
    dma_channel_configure(uart_tx_dma_chan, &c, &uart_get_hw(UART_ID)->dr, src_buffer, 0, false);
}

void uart1_config(uint32_t baudrate, uint8_t databits, uint8_t parity, uint8_t stopbits)
{
    uart_parity_t p = UART_PARITY_NONE;

    /* FTDI: NONE=0, ODD=1, EVEN=2, MARK=3, SPACE=4. The PL011 only knows
     * none/odd/even (mark/space would need stick parity). */
    if (parity == 1) {
        p = UART_PARITY_ODD;
    } else if (parity == 2) {
        p = UART_PARITY_EVEN;
    }

    if (databits < 5 || databits > 8) {
        databits = 8;
    }
    /* FTDI: 0 = 1 stop bit, 1 = 1.5, 2 = 2. */
    uint32_t stop = (stopbits == 2) ? 2 : 1;

    uart_set_baudrate(UART_ID, baudrate);
    uart_set_format(UART_ID, databits, stop, p);
}

/* ------------------------------------------------------------------ */
/* Modem control lines                                                  */
/* ------------------------------------------------------------------ */
void uart1_dtr_rts_init(void)
{
    gpio_init(UART_DTR_PIN);
    gpio_init(UART_RTS_PIN);
    gpio_set_dir(UART_DTR_PIN, GPIO_OUT);
    gpio_set_dir(UART_RTS_PIN, GPIO_OUT);
    gpio_put(UART_DTR_PIN, 1);
    gpio_put(UART_RTS_PIN, 1);
}

void dtr_pin_set(bool status)
{
    gpio_put(UART_DTR_PIN, status);
}

void rts_pin_set(bool status)
{
    gpio_put(UART_RTS_PIN, status);
}

/* ------------------------------------------------------------------ */
/* TX path                                                              */
/* ------------------------------------------------------------------ */
void uart_send_from_ringbuffer(void)
{
    if (rb_empty(&usb_rx_rb)) {
        return;
    }
    if (dma_channel_is_busy(uart_tx_dma_chan)) {
        return;
    }

    uint32_t count = rb_read(&usb_rx_rb, src_buffer, UART_TX_DMA_SIZE);
    if (count) {
        dma_channel_set_read_addr(uart_tx_dma_chan, src_buffer, false);
        dma_channel_set_trans_count(uart_tx_dma_chan, count, true);
        led_toggle(0); /* TX indication */
    }
}

void uart_bridge_flush(void)
{
    usb_rx_rb.tail = usb_rx_rb.head;
    uart1_rx_rb.tail = uart1_rx_rb.head;
}
