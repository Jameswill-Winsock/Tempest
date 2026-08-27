#!/usr/bin/env bash
# One-shot toolchain bootstrap: ARM GCC + pico-sdk into ./.toolchain
# (skips anything already present). Then:
#
#   source setup.sh          # or: . setup.sh
#   cmake -S . -B build -G Ninja -DPICO_BOARD=pico -DTARGET_BOARD=shrike_lite
#   cmake --build build
set -e

TC="${TC:-$HOME/.toolchain}"
GCC_VER="13.2.Rel1"
GCC_DIR="$TC/arm-gnu-toolchain-${GCC_VER}-x86_64-arm-none-eabi"
SDK_DIR="$TC/pico-sdk"

mkdir -p "$TC"

if [ ! -d "$GCC_DIR" ]; then
    echo "==> downloading arm-none-eabi-gcc ${GCC_VER}"
    curl -sSL -o "$TC/gcc.tar.xz" \
        "https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz"
    # this is made fixed for just reproducibility; you can change it if you want to, though code compiling may no longer be guaranteed (duh)
    tar xf "$TC/gcc.tar.xz" -C "$TC"
    rm -f "$TC/gcc.tar.xz"
fi

if [ ! -d "$SDK_DIR/src" ]; then
    echo "==> cloning pico-sdk 2.1.1"
    rm -rf "$SDK_DIR"
    git clone -b 2.1.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
    (cd "$SDK_DIR" && git submodule update --init --depth 1 lib/tinyusb)
fi

export PATH="$GCC_DIR/bin:$PATH"
export PICO_SDK_PATH="$SDK_DIR"

echo "==> $(arm-none-eabi-gcc --version | head -1)"
echo "==> PICO_SDK_PATH=$PICO_SDK_PATH"
echo "(cmake and ninja must be on PATH: apt install cmake ninja-build, or pip install cmake ninja)"
