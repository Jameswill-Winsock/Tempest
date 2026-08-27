#!/usr/bin/env bash
# Builds and runs the host-side MPSSE test bench.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/build"
mkdir -p "$out"
gcc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -DJTAG_SIM=1 -DJTAG_TCK_DELAY=0 \
    -I"$here/../src" \
    "$here/mpsse_sim_test.c" "$here/../src/mpsse_jtag.c" \
    -o "$out/mpsse_sim_test"
"$out/mpsse_sim_test"
