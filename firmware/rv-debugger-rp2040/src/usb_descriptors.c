/**
 * @file usb_descriptors.c
 * @brief Byte-exact FT2232D descriptor set, transcribed from
 *        RV-Debugger-BL702/firmware/app/usb2uartjtag/usb_descriptor.c
 *        and usbd_ftdi.c (Copyright (c) 2021 Sipeed team, Apache-2.0).
 *
 * Nothing here may be "cleaned up": libftdi/FTD2XX and the Gowin programmer
 * identify the chip from bcdDevice (0x0500 => FT2232C/D), the two
 * vendor-specific interfaces and the EEPROM contents. Any deviation changes
 * how the host driver talks to us.
 */
#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"
#include "usbd_ftdi.h"

/* ------------------------------------------------------------------ */
/* Emulated 93C46 EEPROM image (read back word-wise by SIO_READ_EEPROM) */
/* ------------------------------------------------------------------ */
/* Verbatim from upstream usb_descriptor.c. Word 2 (0x6010) doubles as the
 * canned answer to SIO_POLL_MODEM_STATUS_REQUEST, exactly as upstream. */
const uint16_t ftdi_eeprom_info[64] = {
    0x0800, 0x0403, 0x6010, 0x0500, 0x3280, 0x0000, 0x0200, 0x1096,
    0x1aa6, 0x0000, 0x0046, 0x0310, 0x004f, 0x0070, 0x0065, 0x006e,
    0x002d, 0x0045, 0x0043, 0x031a, 0x0055, 0x0053, 0x0042, 0x0020,
    0x0044, 0x0065, 0x0062, 0x0075, 0x0067, 0x0067, 0x0065, 0x0072,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1027
};

/* ------------------------------------------------------------------ */
/* Device descriptor                                                    */
/* ------------------------------------------------------------------ */
static const uint8_t desc_device[] = {
    0x12,                       /* bLength            */
    TUSB_DESC_DEVICE,           /* bDescriptorType    */
    0x00, 0x02,                 /* bcdUSB    2.00     */
    0x00,                       /* bDeviceClass       */
    0x00,                       /* bDeviceSubClass    */
    0x00,                       /* bDeviceProtocol    */
    0x40,                       /* bMaxPacketSize0 64 */
    0x03, 0x04,                 /* idVendor  0x0403   */
    0x10, 0x60,                 /* idProduct 0x6010   */
    0x00, 0x05,                 /* bcdDevice 0x0500 -> FT2232C/D */
    0x01,                       /* iManufacturer      */
    0x02,                       /* iProduct           */
    0x03,                       /* iSerialNumber      */
    0x01                        /* bNumConfigurations */
};

/* ------------------------------------------------------------------ */
/* Configuration descriptor (wTotalLength = 0x37 = 55)                  */
/* ------------------------------------------------------------------ */
static const uint8_t desc_configuration[] = {
    /* Configuration */
    0x09,                       /* bLength             */
    TUSB_DESC_CONFIGURATION,    /* bDescriptorType     */
    0x37, 0x00,                 /* wTotalLength        */
    0x02,                       /* bNumInterfaces      */
    0x01,                       /* bConfigurationValue */
    0x00,                       /* iConfiguration      */
    0xa0,                       /* bmAttributes: bus powered + remote wakeup */
    0x2d,                       /* bMaxPower 90mA      */

    /* Interface 0 - FTDI port A - MPSSE / JTAG */
    0x09,                       /* bLength             */
    TUSB_DESC_INTERFACE,        /* bDescriptorType     */
    0x00,                       /* bInterfaceNumber    */
    0x00,                       /* bAlternateSetting   */
    0x02,                       /* bNumEndpoints       */
    0xff,                       /* bInterfaceClass     */
    0xff,                       /* bInterfaceSubClass  */
    0xff,                       /* bInterfaceProtocol  */
    0x02,                       /* iInterface          */

    0x07,                       /* bLength             */
    TUSB_DESC_ENDPOINT,         /* bDescriptorType     */
    JTAG_IN_EP,                 /* bEndpointAddress 0x81 */
    0x02,                       /* bmAttributes: bulk  */
    0x40, 0x00,                 /* wMaxPacketSize 64   */
    0x01,                       /* bInterval           */

    0x07,                       /* bLength             */
    TUSB_DESC_ENDPOINT,         /* bDescriptorType     */
    JTAG_OUT_EP,                /* bEndpointAddress 0x02 */
    0x02,                       /* bmAttributes: bulk  */
    0x40, 0x00,                 /* wMaxPacketSize 64   */
    0x01,                       /* bInterval           */

    /* Interface 1 - FTDI port B - UART */
    0x09,                       /* bLength             */
    TUSB_DESC_INTERFACE,        /* bDescriptorType     */
    0x01,                       /* bInterfaceNumber    */
    0x00,                       /* bAlternateSetting   */
    0x02,                       /* bNumEndpoints       */
    0xff,                       /* bInterfaceClass     */
    0xff,                       /* bInterfaceSubClass  */
    0xff,                       /* bInterfaceProtocol  */
    0x00,                       /* iInterface          */

    0x07,                       /* bLength             */
    TUSB_DESC_ENDPOINT,         /* bDescriptorType     */
    CDC_IN_EP,                  /* bEndpointAddress 0x83 */
    0x02,                       /* bmAttributes: bulk  */
    0x40, 0x00,                 /* wMaxPacketSize 64   */
    0x01,                       /* bInterval           */

    0x07,                       /* bLength             */
    TUSB_DESC_ENDPOINT,         /* bDescriptorType     */
    CDC_OUT_EP,                 /* bEndpointAddress 0x04 */
    0x02,                       /* bmAttributes: bulk  */
    0x40, 0x00,                 /* wMaxPacketSize 64   */
    0x01                        /* bInterval           */
};

/* ------------------------------------------------------------------ */
/* String descriptors                                                   */
/* ------------------------------------------------------------------ */
/* String 0: LANGID (0x0409) */
static const uint16_t desc_str_langid[] = { (uint16_t)((TUSB_DESC_STRING << 8) | 0x04), 0x0409 };

/* String 1: manufacturer "SIPEED"          (bLength 0x0E) */
static const uint16_t desc_str_manufacturer[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | 0x0E),
    'S', 'I', 'P', 'E', 'E', 'D'
};

/* String 2: product, also iInterface of port A (bLength 0x1C) */
#if FTDI_PRODUCT_GOWIN_CABLE
/* Some Gowin Programmer builds filter the device list on the product
 * string. Enable -DFTDI_PRODUCT_GOWIN_CABLE=1 only if your IDE refuses to
 * list the probe with the stock string. */
static const uint16_t desc_str_product[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | 0x32),
    'G', 'o', 'w', 'i', 'n', ' ', 'U', 'S', 'B', '2', '.', '0',
    ' ', 'D', 'e', 'b', 'u', 'g', ' ', 'C', 'a', 'b', 'l', 'e'
};
#else
static const uint16_t desc_str_product[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | 0x1C),
    'J', 'T', 'A', 'G', ' ', 'D', 'e', 'b', 'u', 'g', 'g', 'e', 'r'
};
#endif

/* String 3: serial number (bLength 0x30 = 23 UTF-16 code units).
 *
 * Upstream ships "FactoryAIOT Prog " followed by the six code units
 * 0x0000 0x0011 0x0022 0x0033 0x0044 0x0055 (the chip-id derived suffix is
 * commented out in main.c). The literal prefix is kept because vendor tools
 * are known to match on it; the suffix can optionally be replaced by this
 * board's unique flash id so several probes can be told apart. */
static uint16_t desc_str_serial[] = {
    (uint16_t)((TUSB_DESC_STRING << 8) | 0x30),
    'F', 'a', 'c', 't', 'o', 'r', 'y', 'A', 'I', 'O', 'T', ' ',
    'P', 'r', 'o', 'g', ' ',
    0x0000, 0x0011, 0x0022, 0x0033, 0x0044, 0x0055
};

void usb_descriptor_init(void)
{
#if FTDI_UNIQUE_SERIAL
    /* Replace the six trailing code units with hex digits of the RP2040
     * board id - this is what upstream's (disabled) chip-id code intended. */
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 3; i++) {
        desc_str_serial[18 + i * 2 + 0] = (uint16_t)hex[(id.id[5 + i] >> 4) & 0x0f];
        desc_str_serial[18 + i * 2 + 1] = (uint16_t)hex[id.id[5 + i] & 0x0f];
    }
#endif
}

/* ------------------------------------------------------------------ */
/* TinyUSB callbacks                                                    */
/* ------------------------------------------------------------------ */
uint8_t const *tud_descriptor_device_cb(void)
{
    return desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    switch (index) {
        case 0:  return desc_str_langid;
        case 1:  return desc_str_manufacturer;
        case 2:  return desc_str_product;
        case 3:  return desc_str_serial;
        default: return NULL; /* stall - same as upstream (no such string) */
    }
}
