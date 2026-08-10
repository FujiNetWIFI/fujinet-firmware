/*
 * ============================================================================
 *  Title:    Intellivoice / SP0256 Emulation interface
 *  Author:   J. Zbiciak
 *  Minty integration wrapper adjustments: 2026
 * ============================================================================
 *  This header is adapted for integration in the Minty firmware project.
 *
 * ============================================================================
 */
#ifndef IVOICE_H_
#define IVOICE_H_

#include <stdint.h>

#ifndef INLINE
#define INLINE inline
#endif

#ifndef SCBUF_SIZE
#define SCBUF_SIZE   (4096)             /* Must be power of 2               */
#endif
#define SCBUF_MASK   (SCBUF_SIZE - 1)

typedef struct lpc12_t
{
    int      rpt, cnt;        /* Repeat counter, period down-counter.       */
    uint32_t per, rng;        /* Period and random number generator.        */
    int      amp;             /* Decoded amplitude.                         */
    int16_t  f_coef[6];       /* F0 through F5.                             */
    int16_t  b_coef[6];       /* B0 through B5.                             */
    int16_t  z_data[6][2];    /* Time-delay data for filter stages.         */
    uint8_t  r[16];           /* Encoded register set.                      */
    int      interp;          /* Interpolation state.                       */
} lpc12_t;

typedef struct ivoice_t
{
    uint64_t    now;

    int         silent;       /* Flag: Intellivoice is silent.              */

    int16_t     scratch[SCBUF_SIZE];    /* Scratch buffer for audio.        */
    uint32_t    sc_head;      /* Scratch circular buffer head.              */
    uint32_t    sc_tail;      /* Scratch circular buffer tail.              */
    uint64_t    sound_current;
    int         sample_frc;
    int         sample_int;

    int         window[24];   /* Sliding window. Enough for up to 96 kHz.   */
    int         wind_sum;     /* Window sum.                                */
    int         wind_ptr;     /* Window pointer.                            */
    int         rate;         /* Output sample rate.                        */
    int         wind;         /* Sliding window size.                       */
    int         pal_mode;     /* 1 = PAL, 0 = NTSC.                         */
    double      time_scale;   /* Time scaling, normally 1.0 in Minty.       */
    double      skipping;     /* Time-scale helper.                         */

    lpc12_t     filt;         /* 12-pole LPC filter.                        */
    int         lrq;          /* Load ReQuest, bit 15 exposed on $0080.     */
    int         ald;          /* Address LoaD command.                      */
    int         pc;           /* Microsequencer PC value.                   */
    int         stack;        /* Microsequencer PC stack.                   */
    int         fifo_sel;     /* True when executing from FIFO.             */
    int         halted;       /* True when SP0256 microsequencer halted.    */
    uint32_t    mode;         /* Mode register.                             */
    uint32_t    page;         /* Page set by SETPAGE.                       */

    uint32_t    fifo_head;    /* FIFO head pointer.                         */
    uint32_t    fifo_tail;    /* FIFO tail pointer.                         */
    uint32_t    fifo_bitp;    /* FIFO bit pointer for partial decles.       */
    uint16_t    fifo[64];     /* 64-decle SPB-640 FIFO.                     */

    int         cur_len;      /* Number of valid samples in cur_buf.        */
    int16_t    *cur_buf;      /* Current sound buffer.                      */
    const uint8_t *rom[16];   /* 4K ROM pages.                              */
} ivoice_t;

/*
 * Save-state support kept for source compatibility with FreeIntv.
 * Minty does not need to use this unless save-state support is later added.
 */

typedef struct ivoiceSerialized
{
    ivoice_t main;
    int      ivoiceBufferSize;
    int16_t  ivoiceBuffer[AUDIO_FREQ / 60 * 2];
} ivoiceSerialized;


extern int      ivoiceBufferSize;
extern int16_t  ivoiceBuffer[];

void ivoiceSerialize(struct ivoiceSerialized *data);
void ivoiceUnserialize(const struct ivoiceSerialized *data);

extern ivoice_t intellivoice;

/*
 * Advance Intellivoice time.
 * len uses the same CPU-side cycle/time base as the original FreeIntv port.
 */
uint32_t ivoice_tk(uint32_t len);

/*
 * Bus accessors.
 * addr is local offset:
 *   0 -> Intellivoice $0080, ALD/LRQ
 *   1 -> Intellivoice $0081, FIFO/FIFO-full
 */
uint32_t ivoice_rd(uint32_t addr);
void     ivoice_wr(uint32_t addr, uint32_t data);

void ivoice_reset(void);
void ivoice_dtor(void);
void ivoice_frame(void);

/*
 * pal_mode: 1 = PAL, 0 = NTSC.
 * time_scale should normally be 1.0 for Minty.
 */
int ivoice_init(int pal_mode, double time_scale);

#endif /* IVOICE_H_ */
