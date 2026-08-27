# Porting notes: BL702 --> RP2040

File-by-file mapping and every deliberate deviation, so the port can be audited against upstream.

## File mapping

| Upstream (`firmware/app/usb2uartjtag/`) | Here (`src/`) |
|---|---|
| `main.c` | `main.c` (LED/init/main loop) + `usbd_ftdi.c` (the `usb_dc_ftdi_send_from_ringbuffer` / `..._receive_to_ringbuffer` endpoint pumps) |
| `usb_descriptor.c` | `usb_descriptors.c` |
| `usbd_ftdi.c` / `.h` | `usbd_ftdi.c` / `.h` |
| `jtag_process.c` (+ `.gowin`) | `mpsse_jtag.c` / `.h`, `jtag_pins.h` |
| `uart_interface.c` / `.h` | `uart_bridge.c` / `.h` |
| `io_cfg.h` | `io_cfg.h` |
| bl_mcu_sdk `Ring_Buffer_Type` | `ring_buffer.h` |
| bl_mcu_sdk USB device stack | TinyUSB application class driver in `usbd_ftdi.c` |

## USB layer

The BL702 build pokes the USB peripheral registers directly (`USB_Set_EPx_Rdy`, `memcopy_to_fifo`, …). On the RP2040 the same wire behaviour is obtained by registering an **application class driver** with TinyUSB (`usbd_app_driver_get_cb`), because none of the stock classes can produce an FTDI endpoint pair:

* `ftdi_driver_open()` claims both vendor-specific interfaces and opens the two bulk pairs, asserting that they really are `0x81/0x02` and `0x83/0x04`.
* All FTDI vendor control requests arrive through `tud_vendor_control_xfer_cb()` - TinyUSB routes *every* vendor-type request there regardless of recipient, which is what FTDI needs (`bmRequestType` `0x40`/`0xC0`, recipient = device).
* `ftdi_in_pump()` reproduces upstream's IN logic: two status bytes + up to 62 payload bytes, and a bare status packet once the latency timer expires so blocking `ftdi_read_data()` calls return instead of timing out.
* OUT flow control matches upstream: the MPSSE endpoint is **not** re-armed until the whole packet has been consumed (upstream's `jtag_received_flag`), and the UART endpoint NAKs while the TX ring has less than 64 bytes free (upstream's `overflow_flag`).

## MPSSE engine

`mpsse_jtag.c` is a direct transcription. The switch, the state numbering, the opcode groups, `mpsse_longlen` / `mpsse_shortlen` handling (including the deliberately odd `if (len == 0) state = IDLE; len--;` ordering and the `while ((shortlen--) > 0)` underflow) are unchanged.

Deliberate differences:

1. **Pin access.** `*(volatile uint32_t *)0x40000188 |= (1 << PIN)` becomes `sio_hw->gpio_set = MASK`. Same single-cycle semantics; the RP2040 has separate set/clr registers so it is actually atomic (the BL702 version was a read-modify-write). Access goes through `jtag_pins.h` so the host test bench can substitute a simulated TAP.
2. **Whole packet per call.** Upstream advances the state machine one byte per main-loop iteration; here `jtag_process()` drains the whole 64-byte packet in one go. Equivalent: a 64-byte command block emits at most 64 response bytes and the response ring holds 1 KiB. The test bench replays streams split at every offset from 1 to 7 bytes to prove the resumable state is still correct.
3. **No global IRQ disable.** Upstream wraps each step in `cpu_global_irq_disable()`. JTAG is fully synchronous, so a USB interrupt that stretches a TCK half-period is harmless - and on the RP2040 blocking IRQs would hurt USB servicing. The hot path is `__not_in_flash_func()` so timing does not depend on XIP cache hits (upstream used TCM for the same reason).
4. **TCK padding.** The BL702 loop lands near 5 MHz; the RP2040 at 125 MHz would be faster, so each half-period is padded with `JTAG_TCK_DELAY` nops.
5. **Gowin quirk.** The PWM channel that free-runs TCK during `MPSSE_RUN_TEST` is an RP2040 PWM slice on the TCK GPIO (`GOWIN_RUNTEST_TCK_HZ`, default 2.5 MHz ≈ the BL702's `period = 29` setting). TCK is handed to the PWM function and returned to SIO afterwards, exactly as upstream does.

### TDO sampling

Upstream samples TDO immediately after driving TCK high. The RP2040 GPIO input path has a two-cycle synchroniser, so the value read is the pin state from ~16 ns earlier - i.e. the value the target presented on the previous falling edge, which is precisely the bit a rising-edge sample should return. 
No change was needed, but this is the thing to check first with a scope if a target returns shifted data.

## UART bridge

* RX: PL011 RX + receive-timeout interrupts (upstream `UART_RX_FIFO_IT | UART_RTO_IT`), FIFO threshold at 1/2, pushing into an 8 KiB ring.
* TX: a DMA channel paced by the UART TX DREQ, refilled from a 4 KiB staging buffer - the same shape as upstream's LLI-based DMA path.
* DTR/RTS are plain GPIO outputs, inverted in `main.c` exactly like upstream (`dtr_pin_set(!dtr)`).
* Mark/space parity cannot be expressed by the PL011 without stick-parity fiddling and falls back to none.

## Things intentionally *not* "fixed"

These look like bugs but are part of the observable behaviour hosts were tested against, so they were kept:

* `0x86` (set clock divisor) is swallowed - TCK stays fixed.
* `0x81`/`0x83` answer `cmd - 0x80` rather than real pin state.
* `0x84`/`0x85` (loopback) do nothing.
* The dead `if (baudrate > 100000 && baudrate < 12000)` clamp in the divisor decoder.
* `SIO_SET_BITMODE` is accepted and ignored (port A is always MPSSE).

The one behavioural repair is the port-B latency timer: upstream compares an endpoint *index* against an endpoint *address* (`ep_idx == CDC_IN_EP`, i.e. `3 == 0x83`), which is never true, so the UART port also used the 1 ms path. Here port B honours the latency timer the host actually set. Set `JTAG_STATUS_POLL_MS`-style behaviour back by forcing `latency_timer2 = 1` if you want bit-identical timing.

## If something misbehaves

1. Check the enumeration first: `lsusb -v -d 0403:6010` should show `bcdDevice 5.00`, two interfaces, four bulk endpoints, and the strings `SIPEED` / `JTAG Debugger`.
2. `dmesg` should show `ftdi_sio` binding **two** ports; the host tool then detaches interface A via libusb.
3. Scope TCK/TMS/TDI/TDO. If the frequency is too high for your target, raise `JTAG_TCK_DELAY`.
4. If a Gowin internal-flash device programs but never leaves the run-test phase, flash the `GOWIN_INT_FLASH_QUIRK` image.
5. `./tests/run_tests.sh` after any change to the state machine.
