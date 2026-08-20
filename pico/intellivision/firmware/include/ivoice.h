/*
 * Minty-only Intellivoice / SP0256 interface.
 * Direct native-rate 10 kHz synthesis, no generic output buffer or resampler.
 */
#ifndef IVOICE_H_
#define IVOICE_H_

#include <stdint.h>

typedef struct lpc12_t
{
    int      rpt;
    int      cnt;
    uint32_t per;
    uint32_t rng;
    int      amp;
    int16_t  f_coef[6];
    int16_t  b_coef[6];
    int16_t  z_data[6][2];
    uint8_t  r[16];
    int      interp;
} lpc12_t;

typedef struct ivoice_t
{
    int         silent;
    lpc12_t     filt;

    int         lrq;
    int         ald;
    int         pc;
    int         stack;
    int         fifo_sel;
    int         halted;
    uint32_t    mode;
    uint32_t    page;

    uint32_t    fifo_head;
    uint32_t    fifo_tail;
    uint32_t    fifo_bitp;
    uint16_t    fifo[64];

    const uint8_t *rom[16];
} ivoice_t;

extern ivoice_t intellivoice;

int16_t  ivoice_minty_next_sample(void);
uint32_t ivoice_rd(uint32_t addr);
void     ivoice_wr(uint32_t addr, uint32_t data);
int      ivoice_init(int pal_mode, double time_scale);

#endif /* IVOICE_H_ */
