#include <string.h>

#include <os7.h>

#include "fujilib.h"

/* Every mailbox access is a read whose address is the message. sccz80 drops a discarded
 * read, volatile or not (`(void)FN_REGSEL[reg];` lost the REGSEL of every register write),
 * so each read stores to a volatile sink. Check with `zcc +coleco -O2 -a fujilib.c`. */
volatile unsigned char fn_sink;

#define FN_TOUCH(addr) (fn_sink = *(volatile unsigned char *)(addr))

/* A register write is two reads: arm the register, deliver the value. An NMI between them
 * leaves the armed register waiting. */
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
    unsigned int count = 0;

    while (*prefix && count < total) {
        FN_TOUCH(FN_TXPAGE + (unsigned char)*prefix++);
        count++;
    }
    while (count < total) {
        unsigned char chr = *name++;

        if (chr == 0)
            break;
        FN_TOUCH(FN_TXPAGE + chr);
        count++;
    }
    while (count++ < total)
        FN_TOUCH(FN_TXPAGE);
}

void fn_tx_from_reply(unsigned int off, unsigned int count)
{
    volatile unsigned char *src = FN_REPLY + off;

    while (count--)
        FN_TOUCH(FN_TXPAGE + *src++);
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

void fn_tx(unsigned char byte)
{
    FN_TOUCH(FN_TXPAGE + byte);
}

void fn_tx_bytes(const unsigned char *src, unsigned int count)
{
    while (count--)
        FN_TOUCH(FN_TXPAGE + *src++);
}

void fn_tx_padded(const char *str, unsigned int total)
{
    unsigned int count = 0;

    while (*str && count < total) {
        FN_TOUCH(FN_TXPAGE + (unsigned char)*str++);
        count++;
    }
    while (count++ < total)
        FN_TOUCH(FN_TXPAGE);
}

void fn_param8(unsigned char value)
{
    FN_TOUCH(FN_TXPAGE + 1);
    FN_TOUCH(FN_TXPAGE + value);
    fn_regwr(FNR_NPARAM, ++nparam);
}

void fn_param16(unsigned int value)
{
    FN_TOUCH(FN_TXPAGE + 2);
    FN_TOUCH(FN_TXPAGE + (unsigned char)(value & 0xFF));
    FN_TOUCH(FN_TXPAGE + (unsigned char)(value >> 8));
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

/* The swap stub runs from our BSS, since the swap replaces all of $8000-$FFFF. The vblank
 * NMI can't be masked and vectors to $8021, the next image's byte, so the stub turns off
 * the VDP interrupt and clears the latched flag before it swaps. It then resets every VDP
 * register and silences the sound chip: the $55AA path initialises neither for the game.
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
