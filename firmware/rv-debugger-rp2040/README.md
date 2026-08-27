# RV-Debugger-RP2040

A port of [sipeed/RV-Debugger-BL702](https://github.com/sipeed/RV-Debugger-BL702) (the `usb2uartjtag` application) to the **Raspberry Pi RP2040**.

Like the original it emulates an **FTDI FT2232D**: interface A is an MPSSE JTAG engine, interface B is a USB↔UART bridge. Nothing on the host has to change; Gowin Programmer, openFPGALoader, OpenOCD's `ftdi` driver, `libftdi` and the FTDI D2XX/VCP drivers all see a plain `0403:6010` FT2232D.

```
        ┌──────────── RP2040 (Pico) ────────────┐
USB ────┤ IF0  EP 0x81/0x02 ── MPSSE ── TCK/TDI/TDO/TMS --> target JTAG
        │ IF1  EP 0x83/0x04 ── UART  ── TX/RX/DTR/RTS   --> target UART
        └───────────────────────────────────────┘
```

## Why it is a *faithful* port, not a rewrite

The requirement was exactness because Gowin's IDE is picky about how the cable behaves, so the parts that are on the wire were transcribed byte for byte rather than reimplemented:

| Item | Status |
|---|---|
| Device/config descriptors | byte-identical to upstream (`bcdDevice 0x0500` ⇒ FT2232C/D, 2 vendor-specific interfaces, EP 0x81/0x02 + 0x83/0x04, 64-byte bulk, `bmAttributes 0xa0`, `bMaxPower 0x2d`) |
| String descriptors | `SIPEED` / `JTAG Debugger` / `FactoryAIOT Prog …`: same lengths, same contents |
| Emulated 93C46 EEPROM | the same 64 words, served by `SIO_READ_EEPROM` |
| FTDI vendor requests | same set, same answers (incl. the canned `0x10 0x60` modem status and the latency-timer defaults `0x04` --> `0x10` on reset) |
| Baud-rate divisor decoding | verbatim, including upstream's dead-code clamp |
| 2-byte status header on every IN packet | `{0x01, 0x60}`, plus bare status packets when idle |
| MPSSE state machine | 1:1 transcription - same 13 states, same opcode table, same bit ordering, same "which opcodes echo data" rules, same `0xFA` bad-command protocol |
| Gowin internal-flash quirk | ported from `jtag_process.c.gowin` (free-running TCK on a PWM slice during long run-test blocks) |

## Pinout (Raspberry Pi Pico default)

| Signal | GPIO | Pin | Notes |
|---|---|---|---|
| TCK | GP2 | 4 | FTDI ADBUS0 |
| TDI | GP3 | 5 | ADBUS1 |
| TDO | GP4 | 6 | ADBUS2, input |
| TMS | GP5 | 7 | ADBUS3 |
| UART TX | GP8 | 11 | --> target RX |
| UART RX | GP9 | 12 | <--> target TX |
| DTR | GP10 | 14 | GPIO, driven by `SIO_SET_MODEM_CTRL` |
| RTS | GP11 | 15 | GPIO |
| LED0 | GP25 | - | on-board LED, RX/TX activity |
| LED1 | GP6 | 9 | optional external LED (active low), power/ready |
| GND | - | 3/8/13/… | **tie to target ground** |

Change any of these in [`src/io_cfg.h`](src/io_cfg.h). Keep TCK/TDI/TDO/TMS contiguous if you ever want to add a PIO engine.

> The RP2040 is 3.3 V and **not** 5 V tolerant. For 1.8 V/2.5 V FPGAs use a level shifter, exactly as with the original board.

## Flashing

Pre-built images are in [`prebuilt/`](prebuilt/):

| File | Board | Notes |
|---|---|---|
| `rv-debugger-rp2040.uf2` | Pico / plain RP2040 | start here |
| `rv-debugger-rp2040-gowin-quirk.uf2` | Pico / plain RP2040 | + `GOWIN_INT_FLASH_QUIRK` |
| `rv-debugger-shrike-lite.uf2` | Vicharak Shrike Lite | JTAG on GP16-GP19 |
| `rv-debugger-shrike-lite-gowin-quirk.uf2` | Vicharak Shrike Lite | + `GOWIN_INT_FLASH_QUIRK` |

The `gowin-quirk` variants free-run TCK on a PWM slice during long run-test-idle blocks, which some internal-flash Gowin parts (GW1N/GW1NZ) need. **Not** required for a Tang Primer 20K (GW2A-18C, external flash), which is what Tempest uses.

Hold **BOOTSEL**, plug the board in, drop the `.uf2` on the `RPI-RP2` drive.

## Building

```bash
export PICO_SDK_PATH=/path/to/pico-sdk      # tested with SDK 2.1.1
cmake -S . -B build -G Ninja -DPICO_BOARD=pico
cmake --build build
# -> build/rv_debugger_rp2040.uf2
```

### Build options

| Option | Default | Meaning |
|---|---|---|
| `JTAG_TCK_DELAY` | `3` | nops padding each TCK half-period. `0` ≈ 8 MHz, `3` ≈ 5 MHz (BL702-like). Lower = faster, raise it if a long/ugly cable misbehaves. |
| `GOWIN_INT_FLASH_QUIRK` | `OFF` | Hand TCK to a PWM slice for clock-only blocks ≥ 8000 bytes (upstream `jtag_process.c.gowin`). |
| `FTDI_PRODUCT_GOWIN_CABLE` | `OFF` | Advertise `Gowin USB2.0 Debug Cable` instead of `JTAG Debugger`, for tools that filter on the product string. |
| `FTDI_UNIQUE_SERIAL` | `OFF` | Derive the serial suffix from the board's unique ID (tell two probes apart). |
| `FTDI_IMPLEMENT_PURGE` | `ON` | Honour `SIO_RESET` purge requests like real silicon (upstream no-ops them). |

```bash
cmake -S . -B build -G Ninja -DGOWIN_INT_FLASH_QUIRK=ON -DJTAG_TCK_DELAY=6
```

## Board profiles

Select with `-DTARGET_BOARD=`; the pin maps live in [`src/io_cfg.h`](src/io_cfg.h).

| | `pico` (default) | `shrike_lite` |
|---|---|---|
| TCK | GP2 | **GP16** |
| TDI | GP3 | **GP17** |
| TDO | GP4 | **GP18** |
| TMS | GP5 | **GP19** |
| UART TX --> target RX | GP8 | GP8 |
| UART RX <-- target TX | GP9 | GP9 |
| DTR / RTS | GP10 / GP11 | GP10 / GP11 |
| LED0 (activity) | GP25 on-board | GP4 user LED |
| LED1 (power) | GP6 external, active low | none (disabled) |

### Vicharak Shrike Lite

The Shrike Lite is an RP2040 **plus an on-board Renesas SLG47910 FPGA**, and the FPGA owns a chunk of the GPIO map - the stock `pico` pinout would have driven TCK/TDI straight into the FPGA's SPI configuration bus. Reserved, do not use for JTAG:

| GPIO | Used by |
|---|---|
| GP0 | FPGA SPI MISO (FPGA pin 6) |
| GP1 | FPGA SPI SS (pin 4) |
| GP2 | FPGA SPI SCK (pin 3) |
| GP3 | FPGA SPI MOSI (pin 5) |
| GP4 | RP2040 user LED |
| GP12 | FPGA PWR |
| GP13 | FPGA EN |
| GP14 | FPGA RESET / FPGA pin 18 |
| GP15 | FPGA pin 17 |

Free on the two 18-pin headers: **GP5-GP11 and GP16-GP22**, which is why JTAG moved to GP16-GP19. GP23-GP29 are avoided (Pico-style SMPS/VBUS/ADC/VSYS functions on some revisions).

```bash
cmake -S . -B build -G Ninja -DPICO_BOARD=pico -DTARGET_BOARD=shrike_lite
cmake --build build
```

or just flash `prebuilt/rv-debugger-shrike-lite.uf2`. Nothing touches the on-board FPGA's pins, so it stays powered down and out of the way.

Question: why for the shrike lite? 
Answer: i forgor to put pin headers on my pico and im too lazy to do it. so im reusing my shrike for this. horribly inelegant but i could not care LESS.

## Wiring to a Sipeed Tang Primer 20K

The core board exposes JTAG + UART on the 8-pin JST SH1.0 connector. **The signal order is silkscreened on the back of the core board** - read it there. I don't expect to spoonfeed you everything.

| Connector signal | Pico |
|---|---|
| TCK | GP2 |
| TDI | GP3 |
| TDO | GP4 |
| TMS | GP5 |
| GND | any GND (**required**) |
| RX (board) | GP8 (our TX) - optional, serial console |
| TX (board) | GP9 (our RX) - optional |
| 3V3 / VCC | leave unconnected, power each board from its own USB |

**DTR/RTS: leave them unconnected.** They are FTDI modem-control lines; on the original hardware they only exist to bit-bang a target MCU's reset/boot pins (the BL702 auto-download trick). Nothing in the JTAG path or in Gowin Programmer touches them. GP10/GP11 stay push-pull outputs driving nothing, which is harmless - or point them at unused GPIOs in `src/io_cfg.h`.

Tang Primer 20K notes:
* The FPGA is a **GW2A-18C** (Arora family, **external** 32 Mbit NOR flash), so use the plain `prebuilt/rv-debugger-rp2040.uf2`. The `GOWIN_INT_FLASH_QUIRK` image is only for internal-flash GW1N/GW1NZ parts.
* On the **Dock** ext-board, DIP switch #1 must be ON or you get *"No Gowin device found"* regardless of the probe.
* The Dock also carries its own onboard BL702 debugger on the same JTAG net. Leave the Dock's debug USB-C unplugged while the external probe is attached, otherwise two drivers fight over TCK/TMS/TDI.
* `openFPGALoader -c ft2232 -b tangprimer20k -f bitstream.fs` (drop `-f` to load into SRAM only). `--freq` has no effect - see the TCK note below.
* Just don't use this thing when you have the dock. Why? Just why? Have some goddamn sense, for god's sake. The BL507 chip is RIGHT there.

## Using it

**Gowin Programmer / Gowin IDE** - plug it in and scan the cable; it appears as an FT2232 with `Cable: Gowin USB2.0 Debug Cable` / FTDI channel A. On Linux make sure `ftdi_sio` releases interface 0 (Gowin uses libusb):

```bash
sudo tee /etc/udev/rules.d/99-rv-debugger.rules <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6010", MODE="0666"
EOF
sudo udevadm control --reload && sudo udevadm trigger
```

Interface B still enumerates as `/dev/ttyUSB*` for the serial console.
I recommend windows. Autoscan works nice. I use fedora, so the programmer keeps crashing on my ubuntu virtual env (dunno why), so I ain't tested it. Hence I can't comment on linux(read Ubuntu here).

**openFPGALoader**

```bash
openFPGALoader -c ft2232 -f bitstream.fs
```

**OpenOCD** - use [`openocd/rv-debugger-rp2040.cfg`](openocd/rv-debugger-rp2040.cfg).

## Tests

The MPSSE engine is not just "it compiles": `tests/` builds the **actual** `src/mpsse_jtag.c` for the host (with `-DJTAG_SIM`, the pin macros swapped for a simulated TAP) and replays real command streams against a behavioural JTAG TAP controller.

```bash
./tests/run_tests.sh
```

covers IDCODE readback through `0x39`/`0x31`, LSB vs MSB bit ordering, left-aligned partial-bit reads, write-only opcodes staying silent, TMS shifts with readback, IR loading + BYPASS, the housekeeping opcodes, the `0xFA` bad-command protocol, and command streams split at every offset across USB packet boundaries.

## Notes, differences and limits

* **TCK frequency is fixed.** Upstream swallows the MPSSE `0x86` divisor command, so `adapter speed` / the IDE's frequency setting has no effect on either the BL702 or this port. Set `JTAG_TCK_DELAY` instead, and verify with a scope - the nop-count → frequency mapping depends on compiler and sysclk.
* **UART port B** is interrupt-driven RX + DMA TX with 8 KiB ring buffers, same sizing as upstream. Parity mark/space is not supported by the PL011 (falls back to none); everything else maps 1:1.
* The latency timer is honoured properly on port B (upstream has an endpoint comparison bug there and always uses 1 ms); port A keeps the 1 ms poll.
* Only the `usb2uartjtag` application was ported. `usb2dualuart` would be a small addition on top of the same USB layer.
* Untested on silicon - verified by simulation and by inspecting the emitted descriptors in the binary. See [`docs/PORTING_NOTES.md`](docs/PORTING_NOTES.md) for the file-by-file mapping and what to check first if something misbehaves.

## Licence

Apache-2.0, inherited from the upstream Sipeed project. Original code Copyright (c) 2021 Sipeed team; the RP2040 port keeps the same terms.
