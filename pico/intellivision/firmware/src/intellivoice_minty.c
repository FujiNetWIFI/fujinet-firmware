#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "audio.h"
#include "intellivoice_minty.h"

/*
 * The FreeIntv/jzIntv-derived module exposes the instance as a global in
 * ivoice.c. It is not declared in ivoice.h, so declare it here in order to
 * consume only samples that have actually been generated.
 */
extern ivoice_t intellivoice;

static uint32_t voice_cycle_step_fp = 0;
static uint32_t voice_cycle_frac = 0;


void init_intellivoice(uint8_t tv_mode)
{
    uint32_t callback_rate;
    uint32_t cpu_rate;
    int pal_mode;

    voice_cycle_frac = 0;
    
    pal_mode = (tv_mode == 0) ? 1 : 0;

    printf("Intellivoice init: %s mode\n", pal_mode ? "PAL" : "NTSC");

    ivoice_init(pal_mode, 1.0);
    ivoice_reset();

    /* Audio callback frequency is the audio callback frequency. */
    callback_rate = 1000000u / AUDIO_PERIOD;
    /* CPU rate is the master clock divided by 4, depending on PAL vs. NTSC. */
    cpu_rate = pal_mode?1000000u:894886u;

    /* Fixed-point cycles per audio callback, Q16.16. */
    voice_cycle_step_fp = (uint32_t)(((uint64_t)cpu_rate << 16) / callback_rate);
}

void intellivoice_reset(void)
{
    ivoice_reset();
    voice_cycle_frac = 0;
}

int16_t intellivoice_next_sample(void)
{
    uint32_t cycles;
    int16_t sample = 0;

    voice_cycle_frac += voice_cycle_step_fp;
    cycles = voice_cycle_frac >> 16;
    voice_cycle_frac &= 0xFFFFu;

    if (cycles != 0u)
        ivoice_tk(cycles);

    /*
     * Consume only last generated sample.
     * ivoice.c increments intellivoice.cur_len when it appends to ivoiceBuffer.
     * If the low-level buffer wraps, cur_len is reset to 0
     */
    sample = ivoiceBuffer[intellivoice.cur_len];

    return sample;
}
