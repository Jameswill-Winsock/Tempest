cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(pico_sdk_import.cmake)

project(rv_debugger_rp2040 C CXX ASM)

pico_sdk_init()

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
# Target carrier board: "pico" (any plain RP2040 board) or "shrike_lite"
# (Vicharak Shrike Lite - RP2040 + SLG47910 FPGA, which reserves GPIO0-4 and
# GPIO12-15 for the on-board FPGA). See src/io_cfg.h.
set(TARGET_BOARD "pico" CACHE STRING "Carrier board: pico | shrike_lite")
set_property(CACHE TARGET_BOARD PROPERTY STRINGS pico shrike_lite)
# Half-period padding for the bit-banged TCK, in nops. 0 = as fast as the
# CPU allows (~8 MHz at 125 MHz sysclk), 3 ~= 5 MHz which matches the BL702.
set(JTAG_TCK_DELAY 3 CACHE STRING "TCK half-period padding, in nops")

# Hand TCK to a PWM channel during long run-test-idle blocks. Needed by some
# internal-flash Gowin parts (upstream jtag_process.c.gowin).
option(GOWIN_INT_FLASH_QUIRK "Free-running TCK for long run-test blocks" OFF)

# Advertise the product string as "Gowin USB2.0 Debug Cable" instead of the
# stock "JTAG Debugger". Only needed if your IDE filters on the string.
option(FTDI_PRODUCT_GOWIN_CABLE "Use Gowin cable product string" OFF)

# Derive the serial number suffix from the board's unique id instead of the
# fixed bytes upstream ships (useful with more than one probe attached).
option(FTDI_UNIQUE_SERIAL "Unique serial number per board" OFF)

# Honour SIO_RESET purge requests (real FT2232D behaviour; upstream no-ops).
option(FTDI_IMPLEMENT_PURGE "Flush ring buffers on SIO_RESET purge" ON)

add_executable(rv_debugger_rp2040
    src/main.c
    src/usb_descriptors.c
    src/usbd_ftdi.c
    src/mpsse_jtag.c
    src/uart_bridge.c
)

target_include_directories(rv_debugger_rp2040 PRIVATE src)

if (TARGET_BOARD STREQUAL "shrike_lite")
    set(BOARD_SHRIKE_LITE 1)
else()
    set(BOARD_SHRIKE_LITE 0)
endif()
message(STATUS "RV-Debugger target board: ${TARGET_BOARD}")

target_compile_definitions(rv_debugger_rp2040 PRIVATE
    BOARD_SHRIKE_LITE=${BOARD_SHRIKE_LITE}
    JTAG_TCK_DELAY=${JTAG_TCK_DELAY}
    GOWIN_INT_FLASH_QUIRK=$<BOOL:${GOWIN_INT_FLASH_QUIRK}>
    FTDI_PRODUCT_GOWIN_CABLE=$<BOOL:${FTDI_PRODUCT_GOWIN_CABLE}>
    FTDI_UNIQUE_SERIAL=$<BOOL:${FTDI_UNIQUE_SERIAL}>
    FTDI_IMPLEMENT_PURGE=$<BOOL:${FTDI_IMPLEMENT_PURGE}>
)

target_compile_options(rv_debugger_rp2040 PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(rv_debugger_rp2040
    pico_stdlib
    pico_unique_id
    tinyusb_device
    hardware_dma
    hardware_uart
    hardware_pwm
    hardware_clocks
)

# The USB peripheral is ours; keep stdio off the USB port. Set to 1 to get
# printf() debugging on uart0 (GP0/GP1).
pico_enable_stdio_uart(rv_debugger_rp2040 0)
pico_enable_stdio_usb(rv_debugger_rp2040 0)

pico_add_extra_outputs(rv_debugger_rp2040)
