/**
 * @file tusb_config.h
 * @brief TinyUSB configuration for the FT2232D emulation.
 *
 * No stock class driver is used: the FTDI interfaces are implemented by an
 * application class driver (see usbd_ftdi.c / usbd_app_driver_get_cb).
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

/* FT2232D reports a 64 byte control endpoint. */
#define CFG_TUD_ENDPOINT0_SIZE 64

/* All built-in device classes are disabled - we provide our own driver. */
#define CFG_TUD_CDC    0
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    0
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_DFU    0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
