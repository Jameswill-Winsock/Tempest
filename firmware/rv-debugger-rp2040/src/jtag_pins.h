/**
 * @file jtag_pins.h
 * @brief Pin access layer for the MPSSE engine.
 *
 * On the RP2040 these compile down to exactly the same single-cycle SIO
 * register writes the BL702 firmware performed on 0x40000188 / 0x40000180.
 * When JTAG_SIM is defined the accessors become external functions so the
 * state machine can be driven against a simulated JTAG TAP on the host
 * (see tests/).
 */
#ifndef _JTAG_PINS_H
#define _JTAG_PINS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef JTAG_SIM

void jtag_pin_tms(bool level);
void jtag_pin_tdi(bool level);
void jtag_pin_tck(bool level);
bool jtag_pin_tdo(void);

#define TMS_HIGH() jtag_pin_tms(true)
#define TMS_LOW()  jtag_pin_tms(false)
#define TDI_HIGH() jtag_pin_tdi(true)
#define TDI_LOW()  jtag_pin_tdi(false)
#define TCK_HIGH() jtag_pin_tck(true)
#define TCK_LOW()  jtag_pin_tck(false)
#define TDO_READ() jtag_pin_tdo()

#else /* firmware build */

#include "hardware/structs/sio.h"
#include "io_cfg.h"

#define TMS_MASK (1ul << TMS_PIN)
#define TDI_MASK (1ul << TDI_PIN)
#define TCK_MASK (1ul << TCK_PIN)
#define TDO_MASK (1ul << TDO_PIN)

#define TMS_HIGH() (sio_hw->gpio_set = TMS_MASK)
#define TMS_LOW()  (sio_hw->gpio_clr = TMS_MASK)
#define TDI_HIGH() (sio_hw->gpio_set = TDI_MASK)
#define TDI_LOW()  (sio_hw->gpio_clr = TDI_MASK)
#define TCK_HIGH() (sio_hw->gpio_set = TCK_MASK)
#define TCK_LOW()  (sio_hw->gpio_clr = TCK_MASK)
#define TDO_READ() (sio_hw->gpio_in & TDO_MASK)

#endif /* JTAG_SIM */

#endif /* _JTAG_PINS_H */
