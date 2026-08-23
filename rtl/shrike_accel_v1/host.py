# shrike_host.py -- RP2040 (MicroPython) host for the Shrike ML-KEM accelerator.
#
# Three runtime-swapped bitstreams (forward / inverse / basemul). This host uses
# the SAME single-frame SPI protocol as the working vendor cordic demo:
#   CS goes LOW, the WHOLE transaction is clocked, then CS goes HIGH.
# (The earlier per-byte-CS version reset the FPGA's bit-frame counter every byte
#  and never worked -- that was the core bring-up bug.)
#
# Reset: real rst_n on GP14, pulsed BEFORE SPI setup, exactly like the vendor demo.

import shrike
from machine import Pin, SPI
import time

Q = 3329
R = 1 << 16

BITSTREAMS = {"forward": "forward.bin", "inverse": "inverse.bin", "basemul": "basemul.bin"}
_current = None

# ---- reset + SPI, vendor style -------------------------------------------------
def _reset():
    rp = Pin(14, Pin.OUT)
    rp.value(0); time.sleep_ms(5)
    rp.value(1); time.sleep_ms(5)

cs  = Pin(1, Pin.OUT, value=1)
spi = SPI(0, baudrate=100_000, polarity=0, phase=0, bits=8,
          firstbit=SPI.MSB, sck=Pin(2), mosi=Pin(3), miso=Pin(0))

# ---- ONE framed transaction: CS low, clock tx, CS high, return rx --------------
def txn(tx_bytes):
    """Send all of tx_bytes in a single CS-low frame; return the rx bytearray."""
    rx = bytearray(len(tx_bytes))
    cs.value(0)
    spi.write_readinto(bytes(tx_bytes), rx)
    cs.value(1)
    return rx

# ---- bitstream control ---------------------------------------------------------
def use(which):
    global _current
    if _current == which:
        return
    shrike.flash(BITSTREAMS[which])   # programs config SRAM, leaves FPGA running
    _current = which
    _reset()                          # real rst_n pulse (vendor sequence)
    time.sleep_ms(5)
    load_zetas(ZETAS)                 # config SRAM (incl. zeta ROM) was rewritten

# ---- framed operations ---------------------------------------------------------
# Loads stream a command byte then all data, in ONE frame (CS stays low).
def _load(opcode, words):
    frame = bytearray()
    frame.append(opcode)
    for w in words:
        frame.append(w & 0xFF)
        frame.append((w >> 8) & 0xFF)
    txn(frame)

def load_zetas(z):  _load(0x20, z)      # 128 words
def load_A(poly):   _load(0x10, poly)   # 256 words
def load_B(poly):   _load(0x11, poly)   # 256 words

def _status():
    # cmd 0x40 + 1 dummy byte in one frame; response comes back in the 2nd byte.
    r = txn([0x40, 0x00])
    return r[1]

def _wait_done(timeout_ms=3000):
    t0 = time.ticks_ms()
    while not (_status() & 1):
        if time.ticks_diff(time.ticks_ms(), t0) > timeout_ms:
            raise RuntimeError("FPGA op timed out")

def _start(opcode):
    txn([opcode])            # single-byte framed command
    _wait_done()

# read back A: cmd 0x50, then discard READ_LEAD pipeline words, then 256 data words
READ_LEAD = 2               # FPGA read pipeline latency, in 16-bit words
def read_A(n=256):
    frame = bytearray()
    frame.append(0x50)
    frame += bytes(2 * (READ_LEAD + n))   # clock out lead+data
    rx = txn(frame)
    out = []
    base = 1 + 2 * READ_LEAD              # skip cmd echo byte + lead words
    for i in range(n):
        lo = rx[base + 2*i]
        hi = rx[base + 2*i + 1]
        out.append(lo | (hi << 8))
    return out

def ntt_forward(): use("forward"); _start(0x30)
def ntt_inverse(): use("inverse"); _start(0x31)
def basemul():     use("basemul"); _start(0x32)

# ---- zeta table ----------------------------------------------------------------
def _brv7(i):
    r = 0
    for k in range(7): r |= ((i >> k) & 1) << (6 - k)
    return r
def _gen_zetas():
    return [(pow(17, _brv7(i), Q) * R) % Q for i in range(128)]
ZETAS = _gen_zetas()

# ---- full poly multiply --------------------------------------------------------
def poly_mul(a, b):
    ntt_forward(); load_A(a); _start(0x30); a_ntt = read_A()
    load_A(b);                _start(0x30); b_ntt = read_A()
    basemul();     load_A(a_ntt); load_B(b_ntt); _start(0x32); c_ntt = read_A()
    ntt_inverse(); load_A(c_ntt); _start(0x31)
    return read_A()

# ---- self-check reference ------------------------------------------------------
def _redc(t):
    m = ((t & 0xFFFF) * 3327) & 0xFFFF
    u = (t + m * Q) >> 16
    return u - Q if u >= Q else u
def _fqmul(a, b): return _redc((a % Q) * (b % Q))
def ref_fwd(v):
    a = [(x * (R % Q)) % Q for x in v]
    for layer in range(7):
        length = 128 >> layer
        for bf in range(128):
            grp = bf // length; i = bf % length
            aa = grp*2*length + i; bb = aa + length
            w = ZETAS[(1 << layer) + grp]
            t = _fqmul(a[bb], w)
            a[aa], a[bb] = (a[aa] + t) % Q, (a[aa] - t) % Q
    Rinv = pow(R, -1, Q)
    return [(x * Rinv) % Q for x in a]

# ---- probes / demos ------------------------------------------------------------
def spi_alive():
    """Write a pattern to A, read it back. Proves the framed SPI link works."""
    use("forward")
    load_A([0x123, 0x0AB, 0x055, 0x0CC] + [0]*252)
    got = read_A(4)
    print("SPI probe (want [291,171,85,204]):", got)
    return got == [0x123, 0x0AB, 0x055, 0x0CC]

def demo_forward():
    use("forward")
    A = [(i*7 + 3) % Q for i in range(256)]
    load_A(A); _start(0x30)
    hw = read_A()
    sw = ref_fwd(A)
    ok = (hw == sw)
    print("FORWARD:", "PASS" if ok else "FAIL", " hw[0:4]=", hw[:4], " exp=", sw[:4])
    return ok

def main():
    print("Shrike ML-KEM accelerator -- forward.bin test")
    if not spi_alive():
        print("SPI link not proven -- check pin map (GPIO12/13/14/15) and CS framing.")
        return
    demo_forward()

if __name__ == "__main__":
    main()