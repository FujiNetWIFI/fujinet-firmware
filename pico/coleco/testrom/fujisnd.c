#include <arch/z80.h>

#include "fujisnd.h"

#define PSG_PORT 0xFF

/* SN76489 command bytes. A latch byte is 1-cc-t-dddd: channel in bits 6:5,
 * type in bit 4 (1 = attenuation, 0 = tone), low four data bits. A following
 * 0-dddddd byte carries the tone's upper six bits. Attenuation 15 is off. */
#define ATTEN(ch, a)  ((unsigned char)(0x90 | ((ch) << 5) | (a)))
#define TONE_LO(ch, n) ((unsigned char)(0x80 | ((ch) << 5) | ((n) & 0x0F)))
#define TONE_HI(n)     ((unsigned char)(((n) >> 4) & 0x3F))

/* 3579545 / (32 * N) Hz. 93 is about 1200 Hz -- high enough to read as a UI
 * click rather than a note. */
#define CLICK_N     93
#define CLICK_ATTEN 4
#define CLICK_TICKS 2       /* vblanks; ~33ms */

static unsigned char ticks;

void snd_init(void)
{
    z80_outp(PSG_PORT, ATTEN(0, 15));
    z80_outp(PSG_PORT, ATTEN(1, 15));
    z80_outp(PSG_PORT, ATTEN(2, 15));
    z80_outp(PSG_PORT, ATTEN(3, 15));
    ticks = 0;
}

void snd_click(void)
{
    z80_outp(PSG_PORT, TONE_LO(0, CLICK_N));
    z80_outp(PSG_PORT, TONE_HI(CLICK_N));
    z80_outp(PSG_PORT, ATTEN(0, CLICK_ATTEN));
    ticks = CLICK_TICKS;
}

void snd_tick(void)
{
    if (ticks != 0 && --ticks == 0)
        z80_outp(PSG_PORT, ATTEN(0, 15));
}
