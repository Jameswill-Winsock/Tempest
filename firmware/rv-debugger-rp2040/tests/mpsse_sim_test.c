/**
 * @file mpsse_sim_test.c
 * @brief Host-side test bench for the MPSSE engine.
 *
 * Compiles the *actual* firmware state machine (src/mpsse_jtag.c, built with
 * -DJTAG_SIM) against a behavioural JTAG TAP controller, then replays the
 * MPSSE command streams a real host (Gowin Programmer / OpenOCD ftdi driver)
 * emits and checks the byte stream that comes back.
 *
 *   build & run:  tests/run_tests.sh
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mpsse_jtag.h"

/* ------------------------------------------------------------------ */
/* JTAG TAP model                                                       */
/* ------------------------------------------------------------------ */
enum tap_state {
    TLR, RTI,
    SEL_DR, CAP_DR, SHIFT_DR, EXIT1_DR, PAUSE_DR, EXIT2_DR, UPD_DR,
    SEL_IR, CAP_IR, SHIFT_IR, EXIT1_IR, PAUSE_IR, EXIT2_IR, UPD_IR
};

static const char *tap_name[] = {
    "TLR", "RTI", "SEL_DR", "CAP_DR", "SHIFT_DR", "EXIT1_DR", "PAUSE_DR",
    "EXIT2_DR", "UPD_DR", "SEL_IR", "CAP_IR", "SHIFT_IR", "EXIT1_IR",
    "PAUSE_IR", "EXIT2_IR", "UPD_IR"
};

/* [state][tms] */
static const enum tap_state tap_next[16][2] = {
    [TLR]      = { RTI,      TLR    },
    [RTI]      = { RTI,      SEL_DR },
    [SEL_DR]   = { CAP_DR,   SEL_IR },
    [CAP_DR]   = { SHIFT_DR, EXIT1_DR },
    [SHIFT_DR] = { SHIFT_DR, EXIT1_DR },
    [EXIT1_DR] = { PAUSE_DR, UPD_DR },
    [PAUSE_DR] = { PAUSE_DR, EXIT2_DR },
    [EXIT2_DR] = { SHIFT_DR, UPD_DR },
    [UPD_DR]   = { RTI,      SEL_DR },
    [SEL_IR]   = { CAP_IR,   TLR },
    [CAP_IR]   = { SHIFT_IR, EXIT1_IR },
    [SHIFT_IR] = { SHIFT_IR, EXIT1_IR },
    [EXIT1_IR] = { PAUSE_IR, UPD_IR },
    [PAUSE_IR] = { PAUSE_IR, EXIT2_IR },
    [EXIT2_IR] = { SHIFT_IR, UPD_IR },
    [UPD_IR]   = { RTI,      SEL_DR },
};

#define IR_LEN     8
#define INSTR_IDCODE 0x09
#define INSTR_BYPASS 0xff
#define TEST_IDCODE 0x0900281bu /* Gowin GW1N-1 style idcode */

static struct {
    enum tap_state state;
    bool tck, tms, tdi, tdo;
    uint32_t dr;
    uint32_t dr_len;
    uint32_t ir_shift;
    uint32_t ir;      /* latched instruction */
    uint64_t tck_edges;
} tap;

static void tap_reset(void)
{
    memset(&tap, 0, sizeof(tap));
    tap.state = TLR;
    tap.ir = INSTR_IDCODE;
    tap.dr = TEST_IDCODE;
    tap.dr_len = 32;
}

static void tap_capture_dr(void)
{
    if (tap.ir == INSTR_IDCODE) {
        tap.dr = TEST_IDCODE;
        tap.dr_len = 32;
    } else {
        tap.dr = 0; /* BYPASS */
        tap.dr_len = 1;
    }
}

/* Pin hooks used by src/mpsse_jtag.c when built with -DJTAG_SIM */
void jtag_pin_tms(bool level) { tap.tms = level; }
void jtag_pin_tdi(bool level) { tap.tdi = level; }
bool jtag_pin_tdo(void)       { return tap.tdo; }

void jtag_pin_tck(bool level)
{
    if (level && !tap.tck) {
        /* rising edge: data is shifted, then the TAP state advances */
        tap.tck_edges++;
        if (tap.state == SHIFT_DR) {
            tap.dr = (tap.dr >> 1) | ((uint32_t)tap.tdi << (tap.dr_len - 1));
        } else if (tap.state == SHIFT_IR) {
            tap.ir_shift = (tap.ir_shift >> 1) | ((uint32_t)tap.tdi << (IR_LEN - 1));
        }

        enum tap_state next = tap_next[tap.state][tap.tms ? 1 : 0];

        if (next == CAP_DR) {
            tap_capture_dr();
        } else if (next == CAP_IR) {
            tap.ir_shift = 0x01; /* mandated capture value */
        } else if (next == UPD_IR) {
            tap.ir = tap.ir_shift;
        } else if (next == TLR) {
            tap.ir = INSTR_IDCODE;
        }
        tap.state = next;
    } else if (!level && tap.tck) {
        /* falling edge: TDO is updated (this is the value the host samples
         * on the next rising edge) */
        if (tap.state == SHIFT_DR) {
            tap.tdo = tap.dr & 1u;
        } else if (tap.state == SHIFT_IR) {
            tap.tdo = tap.ir_shift & 1u;
        } else {
            tap.tdo = false;
        }
    }
    tap.tck = level;
}

/* ------------------------------------------------------------------ */
/* Test harness                                                         */
/* ------------------------------------------------------------------ */
static int failures = 0;
static int checks = 0;

static void feed(const uint8_t *cmd, uint32_t len, uint32_t chunk)
{
    uint32_t off = 0;
    while (off < len) {
        uint32_t n = len - off;
        if (n > chunk) {
            n = chunk;
        }
        memcpy(jtag_rx_buffer_get(), cmd + off, n);
        jtag_rx_submit(n);
        jtag_process();
        off += n;
    }
}

static uint32_t drain(uint8_t *out, uint32_t max)
{
    return rb_read(&jtag_tx_rb, out, max);
}

static void expect_bytes(const char *what, const uint8_t *got, uint32_t got_len,
                         const uint8_t *want, uint32_t want_len)
{
    checks++;
    bool ok = (got_len == want_len) && (memcmp(got, want, want_len) == 0);
    printf("%-46s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) {
        failures++;
        printf("    want:");
        for (uint32_t i = 0; i < want_len; i++) printf(" %02x", want[i]);
        printf("\n    got: ");
        for (uint32_t i = 0; i < got_len; i++) printf(" %02x", got[i]);
        printf("\n");
    }
}

static void expect_state(const char *what, enum tap_state want)
{
    checks++;
    bool ok = tap.state == want;
    printf("%-46s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) {
        failures++;
        printf("    want TAP state %s, got %s\n", tap_name[want], tap_name[tap.state]);
    }
}

static uint8_t bitrev8(uint8_t v)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r = (uint8_t)((r << 1) | ((v >> i) & 1));
    }
    return r;
}

static void setup(void)
{
    tap_reset();
    jtag_ringbuffer_init();
    jtag_gpio_init();
}

/* MPSSE fragments -------------------------------------------------- */
/* 0x4b: clock TMS bits out, no read. length byte = bits-1, data LSB first. */
#define TMS_RESET      0x4b, 0x05, 0x3f /* 6x TMS=1 -> Test-Logic-Reset      */
#define TMS_TO_RTI     0x4b, 0x00, 0x00 /* 1x TMS=0 -> Run-Test/Idle          */
#define TMS_TO_SHIFTDR 0x4b, 0x02, 0x01 /* 1,0,0    -> Select-DR/Capture/Shift*/
#define TMS_TO_SHIFTIR 0x4b, 0x03, 0x03 /* 1,1,0,0  -> Shift-IR               */

int main(void)
{
    uint8_t out[256];
    uint32_t n;

    printf("MPSSE engine tests (state machine from src/mpsse_jtag.c)\n");
    printf("--------------------------------------------------------------\n");

    /* 1: read IDCODE, LSB-first byte shift with read-back (0x39) --------- */
    {
        setup();
        const uint8_t cmd[] = {
            TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR,
            0x39, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, /* 4 bytes in/out */
        };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        const uint8_t want[] = { TEST_IDCODE & 0xff, (TEST_IDCODE >> 8) & 0xff,
                                 (TEST_IDCODE >> 16) & 0xff, (TEST_IDCODE >> 24) & 0xff };
        expect_bytes("IDCODE via 0x39 (byte, LSB first, read)", out, n, want, 4);
        expect_state("  TAP left in Shift-DR", SHIFT_DR);
    }

    /* 2: same, but the command stream is split across USB packets -------- */
    for (uint32_t chunk = 1; chunk <= 7; chunk++) {
        setup();
        const uint8_t cmd[] = {
            TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR,
            0x39, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        feed(cmd, sizeof(cmd), chunk);
        n = drain(out, sizeof(out));
        const uint8_t want[] = { TEST_IDCODE & 0xff, (TEST_IDCODE >> 8) & 0xff,
                                 (TEST_IDCODE >> 16) & 0xff, (TEST_IDCODE >> 24) & 0xff };
        char label[80];
        snprintf(label, sizeof(label), "IDCODE with command stream split /%u", chunk);
        expect_bytes(label, out, n, want, 4);
    }

    /* 3: MSB-first byte shift with read-back (0x31) ---------------------- */
    {
        setup();
        const uint8_t cmd[] = {
            TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR,
            0x31, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        const uint8_t want[] = { bitrev8(TEST_IDCODE & 0xff), bitrev8((TEST_IDCODE >> 8) & 0xff),
                                 bitrev8((TEST_IDCODE >> 16) & 0xff), bitrev8((TEST_IDCODE >> 24) & 0xff) };
        expect_bytes("IDCODE via 0x31 (byte, MSB first, read)", out, n, want, 4);
    }

    /* 4: write-only byte shift (0x19) must not answer -------------------- */
    {
        setup();
        const uint8_t cmd[] = {
            TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR,
            0x19, 0x03, 0x00, 0xaa, 0xbb, 0xcc, 0xdd,
        };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        expect_bytes("0x19 (byte, write only) stays silent", out, n, NULL, 0);
    }

    /* 5: bit-length read (0x3b), 3 bits ---------------------------------- */
    {
        setup();
        const uint8_t cmd[] = {
            TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR,
            0x3b, 0x02, 0x00, /* 3 bits */
        };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        /* FTDI returns partial LSB-first reads left aligned to bit 7 */
        uint8_t want = (uint8_t)((TEST_IDCODE & 0x07) << 5);
        expect_bytes("0x3b (3 bits, LSB first) left-aligned", out, n, &want, 1);
    }

    /* 6: TMS shift with read-back (0x6b) --------------------------------- */
    {
        setup();
        const uint8_t cmd[] = { TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR, 0x6b, 0x00, 0x00 };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        checks++;
        /* one TMS=0 clock in Shift-DR: bit0 of IDCODE, left aligned */
        uint8_t want = (uint8_t)((TEST_IDCODE & 1) << 7);
        expect_bytes("0x6b (TMS shift with read)", out, n, &want, 1);
        checks--;
    }

    /* 7: IR shift, then BYPASS DR is 1 bit wide -------------------------- */
    {
        setup();
        const uint8_t cmd[] = {
            TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTIR,
            /* 7 bits of BYPASS(0xff), last bit goes out with the TMS exit */
            0x1b, 0x06, 0xff,
            0x4b, 0x01, 0x83,       /* TDI=1 held, TMS 1,1 -> Exit1-IR, Update-IR */
            0x4b, 0x01, 0x01,       /* TMS 1,0 -> Select-DR, Capture-DR ... */
            0x4b, 0x00, 0x00,       /* TMS 0 -> Shift-DR */
        };
        feed(cmd, sizeof(cmd), 64);
        (void)drain(out, sizeof(out));
        checks++;
        bool ok = (tap.ir == INSTR_BYPASS) && (tap.dr_len == 1);
        printf("%-46s %s\n", "IR loaded with BYPASS, DR becomes 1 bit", ok ? "PASS" : "FAIL");
        if (!ok) {
            failures++;
            printf("    ir=0x%02x dr_len=%u state=%s\n", tap.ir, tap.dr_len, tap_name[tap.state]);
        }
    }

    /* 8: housekeeping opcodes ------------------------------------------- */
    {
        setup();
        const uint8_t cmd[] = {
            0x80, 0x08, 0x0b, /* set data bits low byte  -> swallowed */
            0x82, 0x00, 0x00, /* set data bits high byte -> swallowed */
            0x86, 0x01, 0x00, /* set clock divisor       -> swallowed */
            0x85,             /* loopback off            -> swallowed */
            0x87,             /* send immediate          -> swallowed */
            0x81,             /* read low byte  -> 0x01 */
            0x83,             /* read high byte -> 0x03 */
        };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        const uint8_t want[] = { 0x01, 0x03 };
        expect_bytes("housekeeping opcodes 0x80..0x87", out, n, want, 2);
    }

    /* 9: bad opcode -> 0xFA + echo (FTDI error protocol) ----------------- */
    {
        setup();
        const uint8_t cmd[] = { 0xab, 0x87 };
        feed(cmd, sizeof(cmd), 64);
        n = drain(out, sizeof(out));
        const uint8_t want[] = { 0xfa, 0xab };
        expect_bytes("bad opcode answers 0xFA + offending byte", out, n, want, 2);
    }

    /* 10: a full 64 byte block of data shifts (packet boundary stress) --- */
    {
        setup();
        uint8_t cmd[3 + 3 + 3 + 3 + 60];
        uint32_t i = 0;
        const uint8_t pre[] = { TMS_RESET, TMS_TO_RTI, TMS_TO_SHIFTDR };
        memcpy(cmd, pre, sizeof(pre));
        i = sizeof(pre);
        cmd[i++] = 0x39;
        cmd[i++] = 0x3b; /* 60 bytes */
        cmd[i++] = 0x00;
        for (uint32_t k = 0; k < 60; k++) {
            cmd[i++] = 0x00;
        }
        feed(cmd, i, 64); /* forces a split mid data-block */
        n = drain(out, sizeof(out));
        checks++;
        bool ok = (n == 60);
        printf("%-46s %s\n", "60 byte shift split over USB packets", ok ? "PASS" : "FAIL");
        if (!ok) {
            failures++;
            printf("    expected 60 response bytes, got %u\n", n);
        }
        /* the first four bytes are still the IDCODE */
        const uint8_t want[] = { TEST_IDCODE & 0xff, (TEST_IDCODE >> 8) & 0xff,
                                 (TEST_IDCODE >> 16) & 0xff, (TEST_IDCODE >> 24) & 0xff };
        expect_bytes("  ... starting with the IDCODE", out, 4, want, 4);
    }

    printf("--------------------------------------------------------------\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
