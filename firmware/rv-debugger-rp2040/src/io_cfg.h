/**
 * @file io_cfg.h
 * @brief Board pinout for the RP2040 port of RV-Debugger-BL702.
 *
 * Port of sipeed/RV-Debugger-BL702 (Apache-2.0, Copyright (c) 2021 Sipeed team)
 * to the Raspberry Pi RP2040.
 *
 * Default target: Raspberry Pi Pico / Pico W (any RP2040 board works, just
 * change the pins below).
 *
 *   FTDI "port A" (interface 0, EP 0x81/0x02) -> MPSSE JTAG
 *   FTDI "port B" (interface 1, EP 0x83/0x04) -> UART bridge
 */
#ifndef _IO_CFG_H
#define _IO_CFG_H

#ifndef BOARD_SHRIKE_LITE
#define BOARD_SHRIKE_LITE 0
#endif

#if BOARD_SHRIKE_LITE

/* ==================================================================== */
/* Vicharak Shrike Lite (RP2040 + Renesas SLG47910 ForgeFPGA)           */
/* ==================================================================== */
/*
 * Careful: this board is not a bare Pico. These RP2040 GPIOs are hard-wired
 * to the on-board FPGA and must NOT be used for JTAG:
 *
 *   GPIO0  FPGA SPI MISO (pin 6)     GPIO12 FPGA PWR
 *   GPIO1  FPGA SPI SS   (pin 4)     GPIO13 FPGA EN
 *   GPIO2  FPGA SPI SCK  (pin 3)     GPIO14 FPGA RESET / FPGA pin 18
 *   GPIO3  FPGA SPI MOSI (pin 5)     GPIO15 FPGA pin 17
 *   GPIO4  RP2040 user LED
 *
 * Free on the 2x18 headers: GPIO5-11 and GPIO16-22. GPIO23-25 and 26-29
 * carry Pico-style special functions (SMPS / VBUS / ADC / VSYS) on some
 * revisions, so they are avoided here.
 */
#define TCK_PIN 16 /* out - FTDI ADBUS0 */
#define TDI_PIN 17 /* out - FTDI ADBUS1 */
#define TDO_PIN 18 /* in  - FTDI ADBUS2 */
#define TMS_PIN 19 /* out - FTDI ADBUS3 */

#define UART_ID       uart1
#define UART_IRQ_ID   UART1_IRQ
#define UART_TXD_PIN  8  /* uart1 TX -> target RX */
#define UART_RXD_PIN  9  /* uart1 RX <- target TX */
#define UART_DTR_PIN  10
#define UART_RTS_PIN  11

/* The only MCU-driven LED is the user LED on GPIO4. There is no second
 * one (the other LED belongs to the FPGA), so LED1 is disabled. */
#define LED0_PIN         4
#define LED0_ACTIVE_LOW  0
#define LED1_PIN         (-1) /* disabled */
#define LED1_ACTIVE_LOW  0

#else /* Raspberry Pi Pico and other plain RP2040 boards */

/* ------------------------------------------------------------------ */
/* JTAG (MPSSE port A)                                                 */
/* ------------------------------------------------------------------ */
/* Keep TCK/TDI/TDO/TMS on consecutive GPIOs: it keeps the bit-bang
 * masks in one register write and leaves the door open for a future
 * PIO engine (which needs contiguous pin groups). */
#define TCK_PIN 2 /* out - FTDI ADBUS0 */
#define TDI_PIN 3 /* out - FTDI ADBUS1 */
#define TDO_PIN 4 /* in  - FTDI ADBUS2 */
#define TMS_PIN 5 /* out - FTDI ADBUS3 */

/* ------------------------------------------------------------------ */
/* UART bridge (port B)                                                */
/* ------------------------------------------------------------------ */
#define UART_ID       uart1
#define UART_IRQ_ID   UART1_IRQ
#define UART_TXD_PIN  8  /* uart1 TX -> target RX */
#define UART_RXD_PIN  9  /* uart1 RX <- target TX */
#define UART_DTR_PIN  10 /* plain GPIO output, driven by SIO_SET_MODEM_CTRL */
#define UART_RTS_PIN  11 /* plain GPIO output, driven by SIO_SET_MODEM_CTRL */

/* ------------------------------------------------------------------ */
/* LEDs                                                                */
/* ------------------------------------------------------------------ */
/* LED0 = RX/TX activity, LED1 = power/ready (same meaning as upstream).
 * Upstream drives both active-low; the Pico's on-board LED is active-high,
 * so polarity is configurable per LED. Set a pin to -1 to disable it. */
#define LED0_PIN         25 /* on-board LED on Pico */
#define LED0_ACTIVE_LOW  0
#define LED1_PIN         6  /* optional external LED */
#define LED1_ACTIVE_LOW  1

#endif /* BOARD_SHRIKE_LITE */

/* ------------------------------------------------------------------ */
/* Debug console                                                       */
/* ------------------------------------------------------------------ */
/* uart0 is left free for printf() debugging (disabled by default, see
 * CMakeLists / pico_enable_stdio_uart).
 * NOTE on Shrike Lite: GPIO0/GPIO1 are the FPGA SPI MISO/SS lines, so do
 * not enable the uart0 console there without moving these pins. */
#define UART0_TXD_PIN 0
#define UART0_RXD_PIN 1

#endif /* _IO_CFG_H */
