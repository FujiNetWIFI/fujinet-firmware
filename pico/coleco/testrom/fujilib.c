#include <string.h>

#include <os7.h>

#include "fujilib.h"

/* Every mailbox access is a READ whose value we do not want -- the address is
 * the message. Writing that as `(void)FN_REGSEL[reg];` looks obviously right
 * and is silently wrong: sccz80 emits the address calculation and then throws
 * the load away, volatile or not. The generated fn_regwr was
 *
 *      ld de,64768     ; $FD00, computed...
 *      ld de,65024     ; ...and immediately overwritten by $FE00
 *
 * -- the REGSEL half of every register write simply did not happen, so no
 * transaction ever launched and the client sat polling ACKSEQ forever. Storing
 * the byte somewhere volatile is what makes the read a side effect the
 * compiler has to keep. Check the generated code if this is ever rewritten:
 * `zcc +coleco -O2 -a fujilib.c` and look for two loads, not one. */
volatile unsigned char fn_sink;

#define FN_TOUCH(addr) (fn_sink = *(volatile unsigned char *)(addr))

/* A register write is exactly two reads, back to back: one to arm the register
 * number, one to deliver the value. The cartridge disarms after a single use,
 * so a stray read of the data page on its own can never change anything --
 * which is what makes the vblank NMI harmless here. The NMI can land between
 * these two lines; the armed register simply waits. */
static void fn_regwr(unsigned char reg, unsigned char val)
{
    FN_TOUCH(FN_REGSEL + reg);
    FN_TOUCH(FN_REGDAT + val);
}

bool fn_present(void)
{
    return FN_MAGIC0 == 'F' && FN_MAGIC1 == 'N';
}

void fn_tx_path(const char *prefix, volatile unsigned char *name,
                unsigned int total)
{
    unsigned int n = 0;

    while (*prefix && n < total) {
        FN_TOUCH(FN_TXPAGE + (unsigned char)*prefix++);
        n++;
    }
    while (n < total) {
        unsigned char c = *name++;

        if (c == 0)
            break;
        FN_TOUCH(FN_TXPAGE + c);
        n++;
    }
    while (n++ < total)
        FN_TOUCH(FN_TXPAGE);
}

void fn_tx_from_reply(unsigned int off, unsigned int n)
{
    volatile unsigned char *r = FN_REPLY + off;

    while (n--)
        FN_TOUCH(FN_TXPAGE + *r++);
}

/* Parameters ride the same stream, each as {size, value...} little-endian,
 * with NPARAM counting them. Interleaving the count update with the stream is
 * safe: the register pages and the TX page are decoded separately, so a
 * REGSEL/REGDATA pair between two payload bytes disturbs nothing. */
static unsigned char nparam;

void fn_start(unsigned char device, unsigned char command)
{
    nparam = 0;
    fn_regwr(FNR_DATA_RST, 0);
    fn_regwr(FNR_DEVICE, device);
    fn_regwr(FNR_CMD, command);
    fn_regwr(FNR_NPARAM, 0);
}

void fn_tx(unsigned char b)
{
    FN_TOUCH(FN_TXPAGE + b);
}

void fn_tx_bytes(const unsigned char *p, unsigned int n)
{
    while (n--)
        FN_TOUCH(FN_TXPAGE + *p++);
}

void fn_tx_padded(const char *s, unsigned int total)
{
    unsigned int n = 0;

    while (*s && n < total) {
        FN_TOUCH(FN_TXPAGE + (unsigned char)*s++);
        n++;
    }
    while (n++ < total)
        FN_TOUCH(FN_TXPAGE);
}

void fn_param8(unsigned char v)
{
    FN_TOUCH(FN_TXPAGE + 1);
    FN_TOUCH(FN_TXPAGE + v);
    fn_regwr(FNR_NPARAM, ++nparam);
}

void fn_param16(unsigned int v)
{
    FN_TOUCH(FN_TXPAGE + 2);
    FN_TOUCH(FN_TXPAGE + (unsigned char)(v & 0xFF));
    FN_TOUCH(FN_TXPAGE + (unsigned char)(v >> 8));
    fn_regwr(FNR_NPARAM, ++nparam);
}

unsigned char fn_commit(void)
{
    unsigned char want = (unsigned char)(FN_ACKSEQ + 1);
    unsigned int outer, inner;

    if (want == 0)
        want = 1;               /* 0 means "never used" */
    nparam = 0;
    fn_regwr(FNR_SEQ, want);

    /* The cartridge's own transaction budget is 5 s (60 s for MOUNT_IMAGE);
     * wait longer than that so a real timeout is reported as the cart's error
     * code rather than as ours. The loop is deliberately dumb: OS7's timers
     * run off the vblank NMI, and a client that has not installed one yet --
     * fujitest has not -- would wait forever on them. */
    for (outer = 0; outer < 2400u; outer++) {
        for (inner = 0; inner < 250u; inner++) {
            if (FN_ACKSEQ == want)
                return FN_ERRCODE;
        }
    }
    return FN_EWAIT;
}

bool fn_acked(void)
{
    return FN_REPLYCMD == FN_ACK;
}

unsigned int fn_reply_len(void)
{
    return (unsigned int)FN_RXLEN_LO | ((unsigned int)FN_RXLEN_HI << 8);
}

/* The swap stub, and why it is shaped like this.
 *
 * The cartridge replaces every byte of $8000-$FFFF between one read and the
 * next, so whatever triggers it must not itself be in cartridge space. It goes
 * in our own BSS -- NOT at a hard-coded $6000, which is where z88dk puts the
 * BSS anyway ($702C mirrors to $602C in the 1K of RAM).
 *
 * The vblank interrupt is on the Z80's /NMI and cannot be masked with DI, and
 * the BIOS vectors it straight to $8021 -- a byte that belongs to the NEXT
 * image the moment we swap. So the stub silences the VDP first, clears the
 * flag that is already latched, lets any in-flight NMI be taken while our own
 * handler is still there, and only then swaps.
 *
 * It also resets the VDP registers before jumping. The BIOS's $55AA path is
 * LD HL,($800A) / JP (HL) with no VDP or RAM initialisation at all, so a game
 * that assumes a cold VDP would otherwise inherit whatever mode CONFIG left
 * behind. Silencing the sound chip is the same courtesy.
 */
static unsigned char stub[64];

static const unsigned char stub_src[] = {
    /* Screen off, VDP interrupt off: write $00 to VDP register 1. */
    0x3E, 0x00,             /* ld  a,$00            */
    0xD3, 0xBF,             /* out ($bf),a          */
    0x3E, 0x81,             /* ld  a,$81            ; register 1 */
    0xD3, 0xBF,             /* out ($bf),a          */
    /* Clear the VDP's latched interrupt flag (and de-assert /NMI). */
    0xDB, 0xBF,             /* in  a,($bf)          */
    /* Give any NMI already in flight a moment to be taken while OUR $8021
     * handler is still the live one. */
    0x00, 0x00, 0x00, 0x00, /* nop x4               */
    0x00, 0x00, 0x00, 0x00,
    /* Silence the SN76489: attenuate all four channels. */
    0x3E, 0x9F, 0xD3, 0xFF, /* ld a,$9f / out ($ff),a */
    0x3E, 0xBF, 0xD3, 0xFF,
    0x3E, 0xDF, 0xD3, 0xFF,
    0x3E, 0xFF, 0xD3, 0xFF,
    /* THE SWAP: one read of $FDFE. Every byte of cartridge space changes
     * between this instruction and the next. */
    0x3A, 0xFE, 0xFD,       /* ld  a,($fdfe)        */
    /* Registers 0..7 to a power-on-like state, so a $55AA game that skips the
     * BIOS title screen does not inherit our video mode. */
    0x21, 0x00, 0x80,       /* ld  hl,$8000         ; h=reg data, l=reg # */
    0x06, 0x08,             /* ld  b,8              */
    0x7D,                   /* vloop: ld a,l        */
    0xD3, 0xBF,             /* out ($bf),a          ; data byte (0) -- see below */
    0x7C,                   /* ld  a,h              */
    0xD3, 0xBF,             /* out ($bf),a          ; $80|reg */
    0x24,                   /* inc h                */
    0x10, 0xF8,             /* djnz vloop           */
    /* Cold start: the BIOS re-reads the new image's $8000 header, so $55AA and
     * $AA55 behave exactly as they would on a real cartridge. */
    0xC3, 0x00, 0x00        /* jp  $0000            */
};

void fn_boot_swap(void)
{
    memcpy(stub, stub_src, sizeof stub_src);
    fn_regwr(FNR_BOOTLOCK, FN_BOOTLOCK_MAGIC);
    ((void (*)(void))stub)();
    for (;;)
        ;                       /* not reached */
}
