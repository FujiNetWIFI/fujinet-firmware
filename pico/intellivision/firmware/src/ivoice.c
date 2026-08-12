//#define SINGLE_STEP
//#define DEBUG
//#define DEBUG_FIFO
/*
 * ============================================================================
 *  Title:    Intellivoice Emulation
 *  Author:   J. Zbiciak
 * ============================================================================
 *  This module actually attempts to emulate the Intellivoice.  Wild!
 * ============================================================================
 *  The Intellivoice is mapped into two locations in memory, $0080-$0081.
 *  (This ignores the separate 8-bit bus that the SPB-640 provides, since
 *  nothing uses it and I haven't emulated it.)
 *
 *  Location $0080 provides an interface to the "Address LoaD" (ALD)
 *  mechanism on the SP0256.  Reads from this address return the current
 *  "Load ReQuest" (LRQ) state in bit 15.  When LRQ is 1 (ie. bit 15 of
 *  location $0080 reads as 1), the SP0256 is ready to receive a new command.
 *  A new command address may then be written to location $0080 to trigger
 *  the playback of a sound.  Note that command address register is actually
 *  a 1-deep FIFO, and so LRQ will go to 1 before the SP0256 is finished
 *  speaking.
 *
 *  Location $0081 provides an interface to the SPB-640's 64-decle speech
 *  FIFO.  Reads from this address return the "FIFO full" state in bit 15.
 *  When bit 15 reads as 0, the FIFO has room for at least 1 more decle.
 *  Writes to this address can either clear the FIFO, or provide new data
 *  to the FIFO.  To clear the FIFO, write a value with Bit 10 == 1.
 *  To put a decle into the FIFO, write a value with Bit 10 == 0.  It's
 *  currently unknown what happens when a program attempts to write to the
 *  FIFO when the FIFO is full.  This emulation drops the extra data.
 *
 *  The exact format of the SP0256 speech data, as well as the overall
 *  system view from the SP0256's perspective is documented elsewhere.
 * ============================================================================
 */

/*#define SINGLE_STEP*/

//#undef DEBUG_FIFO
//#ifdef DEBUG_FIFO
//#define dfprintf(x) printf(x)
//#else
//#define dfprintf(x) 
//#endif

//#ifdef DEBUG
//#define jzdprintf(x) printf(x)
//#else
//#define jzdprintf(x)
//#endif

#undef HIGH_QUALITY
#define PER_PAUSE    (64)               /* Equiv timing period for pauses.  */
#define PER_NOISE    (64)               /* Equiv timing period for noise.   */

#define FIFO_ADDR    (0x1800 << 3)      /* SP0256 address of speech FIFO.   */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/bit_ops.h"
/*
 * Minty port:
 * - remove FreeIntv/libretro dependencies
 */
#include "audio.h"
#include "ivoice.h"

ivoice_t intellivoice;

int ivoiceBufferSize;
int16_t ivoiceBuffer[AUDIO_FREQ / 60 * 2];

void ivoiceSerialize(struct ivoiceSerialized *data)
{
    memcpy(&data->main, &intellivoice, sizeof(intellivoice));
    data->ivoiceBufferSize = ivoiceBufferSize;
    memcpy(data->ivoiceBuffer, ivoiceBuffer, sizeof(ivoiceBuffer));
}

void ivoiceUnserialize(const struct ivoiceSerialized *data)
{
    // Copies everything except the pointers
    memcpy(&intellivoice, &data->main, (unsigned char *) &intellivoice.cur_buf - (unsigned char *) &intellivoice);
    ivoiceBufferSize = data->ivoiceBufferSize;
    memcpy(ivoiceBuffer, data->ivoiceBuffer, sizeof(ivoiceBuffer));
}

/* ======================================================================== */
/*  Internal function prototypes.                                           */
/* ======================================================================== */
static INLINE int16_t  limit (int16_t s);
static int             lpc12_update(lpc12_t *f, int, int16_t *, uint32_t *);
static void            lpc12_regdec(lpc12_t *f);
static uint32_t        sp0256_getb(ivoice_t *ivoice, int len);
static void            sp0256_micro(ivoice_t *iv);

/* ======================================================================== */
/*  IVOICE_QTBL  -- Coefficient Quantization Table.  This comes from a      */
/*                  SP0250 data sheet, and should be correct for SP0256.    */
/* ======================================================================== */
static const int16_t qtbl[128] =
{
    0,      9,      17,     25,     33,     41,     49,     57,
    65,     73,     81,     89,     97,     105,    113,    121,
    129,    137,    145,    153,    161,    169,    177,    185,
    193,    201,    209,    217,    225,    233,    241,    249,
    257,    265,    273,    281,    289,    297,    301,    305,
    309,    313,    317,    321,    325,    329,    333,    337,
    341,    345,    349,    353,    357,    361,    365,    369,
    373,    377,    381,    385,    389,    393,    397,    401,
    405,    409,    413,    417,    421,    425,    427,    429,
    431,    433,    435,    437,    439,    441,    443,    445,
    447,    449,    451,    453,    455,    457,    459,    461,
    463,    465,    467,    469,    471,    473,    475,    477,
    479,    481,    482,    483,    484,    485,    486,    487,
    488,    489,    490,    491,    492,    493,    494,    495,
    496,    497,    498,    499,    500,    501,    502,    503,
    504,    505,    506,    507,    508,    509,    510,    511
};

/* ======================================================================== */
/*  LIMIT            -- Limiter function for digital sample output.         */
/* ======================================================================== */
static INLINE int16_t limit(int16_t s)
{
#ifdef HIGH_QUALITY /* Higher quality than the original, but who cares? */
    if (s >  8191) return  8191;
    if (s < -8192) return -8192;
#else
    if (s >  127) return  127;
    if (s < -128) return -128;
#endif
    return s;
}

#if 0
/* ======================================================================== */
/*  SAMP_MPY         -- Multiply sample w/ coef                             */
/* ======================================================================== */
static int samp_mpy(int coef, int samp)
{
    return coef * samp;
}
#endif

/* ======================================================================== */
/*  AMP_DECODE       -- Decode amplitude register                           */
/* ======================================================================== */
static int amp_decode(uint8_t a)
{
    /* -------------------------------------------------------------------- */
    /*  Amplitude has 3 bits of exponent and 5 bits of mantissa.  This      */
    /*  contradicts USP 4,296,269 but matches the SP0250 Apps Manual.       */
    /* -------------------------------------------------------------------- */
    int expn = (a & 0xE0) >> 5;
    int mant = (a & 0x1F);
    int ampl = mant << expn;

    /* -------------------------------------------------------------------- */
    /*  Careful reading of USP 4,296,279, around line 60 in column 14 on    */
    /*  page 16 of the scan suggests the LSB might be held and injected     */
    /*  into the output while the exponent gets counted down, although      */
    /*  this seems dubious.                                                 */
    /* -------------------------------------------------------------------- */
#if 0
    if (mant & 1)
        ampl |= (1u << expn) - 1;
#endif

    return ampl;
}

/* ======================================================================== */
/*  LPC12_UPDATE     -- Update the 12-pole filter, outputting samples.      */
/* ======================================================================== */
static int lpc12_update(lpc12_t *f, int num_samp, int16_t *out, uint32_t *optr)
{
    int i, j;
    int16_t samp;
    int do_int, bit;
    int oidx = *optr;

    /* -------------------------------------------------------------------- */
    /*  Iterate up to the desired number of samples.  We actually may       */
    /*  break out early if our repeat count expires.                        */
    /* -------------------------------------------------------------------- */
    for (i = 0; i < num_samp; i++)
    {
        /* ---------------------------------------------------------------- */
        /*  Generate a series of periodic impulses, or random noise.        */
        /* ---------------------------------------------------------------- */
        do_int = 0;
        samp   = 0;
        bit    = f->rng & 1;
        f->rng = (f->rng >> 1) ^ (bit ? 0x4001 : 0);

        if (--f->cnt <= 0)
        {
            if (f->rpt-- <= 0)      /* Stop if we expire the repeat counter */
            {
                f->cnt = f->rpt = 0;
                break;
            }

            f->cnt = f->per ? f->per : PER_NOISE;
            samp   = f->amp;
            do_int = f->interp;
        }

        if (!f->per)
            samp   = bit ? -f->amp : f->amp;

        /* ---------------------------------------------------------------- */
        /*  If we need to, process the interpolation registers.             */
        /* ---------------------------------------------------------------- */
        if (do_int)
        {
            f->r[0] += f->r[14];
            f->r[1] += f->r[15];

            f->amp   = amp_decode(f->r[0]);
            f->per   = f->r[1];

            do_int   = 0;
        }

        /* ---------------------------------------------------------------- */
        /*  Each 2nd order stage looks like one of these.  The App. Manual  */
        /*  gives the first form, the patent gives the second form.         */
        /*  They're equivalent except for time delay.  I implement the      */
        /*  first form.   (Note: 1/Z == 1 unit of time delay.)              */
        /*                                                                  */
        /*          ---->(+)-------->(+)----------+------->                 */
        /*                ^           ^           |                         */
        /*                |           |           |                         */
        /*                |           |           |                         */
        /*               [B]        [2*F]         |                         */
        /*                ^           ^           |                         */
        /*                |           |           |                         */
        /*                |           |           |                         */
        /*                +---[1/Z]<--+---[1/Z]<--+                         */
        /*                                                                  */
        /*                                                                  */
        /*                +---[2*F]<---+                                    */
        /*                |            |                                    */
        /*                |            |                                    */
        /*                v            |                                    */
        /*          ---->(+)-->[1/Z]-->+-->[1/Z]---+------>                 */
        /*                ^                        |                        */
        /*                |                        |                        */
        /*                |                        |                        */
        /*                +-----------[B]<---------+                        */
        /*                                                                  */
        /* ---------------------------------------------------------------- */
        for (j = 0; j < 6; j++)
        {
#if 1
            samp += (((int)f->b_coef[j] * (int)f->z_data[j][1]) >> 9);
            samp += (((int)f->f_coef[j] * (int)f->z_data[j][0]) >> 8);
#else
            int temp;
            /*temp  = ((int)f->f_coef[j] * (int)f->z_data[j][0]) << 1;*/
            /*temp += ((int)f->b_coef[j] * (int)f->z_data[j][1]);*/
            static int fdly = 0;

            if (f->f_coef[j] >= 0)
                temp =    2 * f->f_coef[j] * f->z_data[j][0] + fdly;
            else
                temp = -(-2 * f->f_coef[j] * f->z_data[j][0] + fdly);

            fdly  =  1 & (temp >> 26);
            temp +=  samp_mpy(f->b_coef[j], f->z_data[j][1]);
            samp +=  temp >> 9;
#endif

            f->z_data[j][1] = f->z_data[j][0];
            f->z_data[j][0] = samp;
        }

#ifdef HIGH_QUALITY /* Higher quality than the original, but who cares? */
        out[oidx++ & SCBUF_MASK] = limit(samp) * 4;
#else
        out[oidx++ & SCBUF_MASK] = limit(samp >> 4) * 256;
#endif
    }

    *optr = oidx;

    return i;
}

/*static int stage_map[6] = { 4, 2, 0, 5, 3, 1 };*/
/*static int stage_map[6] = { 3, 0, 4, 1, 5, 2 };*/
/*static int stage_map[6] = { 3, 0, 1, 4, 2, 5 };*/
static const int stage_map[6] = { 0, 1, 2, 3, 4, 5 };

/* ======================================================================== */
/*  LPC12_REGDEC -- Decode the register set in the filter bank.             */
/* ======================================================================== */
static void lpc12_regdec(lpc12_t *f)
{
    int i;

    /* -------------------------------------------------------------------- */
    /*  Decode the Amplitude and Period registers.  Force cnt to 0 to get   */
    /*  the initial impulse.  (Redundant?)                                  */
    /* -------------------------------------------------------------------- */
    f->amp = amp_decode(f->r[0]);
    f->cnt = 0;
    f->per = f->r[1];

    /* -------------------------------------------------------------------- */
    /*  Decode the filter coefficients from the quant table.                */
    /* -------------------------------------------------------------------- */
    for (i = 0; i < 6; i++)
    {
        #define IQ(x) (((x) & 0x80) ? qtbl[0x7F & -(x)] : -qtbl[(x)])

        f->b_coef[stage_map[i]] = IQ(f->r[2 + 2*i]);
        f->f_coef[stage_map[i]] = IQ(f->r[3 + 2*i]);
    }

    /* -------------------------------------------------------------------- */
    /*  Set the Interp flag based on whether we have interpolation parms    */
    /* -------------------------------------------------------------------- */
    f->interp = f->r[14] || f->r[15];

    return;
}

/* ======================================================================== */
/*  MASK table                                                              */
/* ======================================================================== */
static const uint8_t mask[4097] =
{
    0xE8, 0xBB, 0xE8, 0x87, 0xE8, 0x17, 0xE8, 0x37, 0xE8, 0xF7, 0xE8, 0x8F,
    0xE8, 0xCF, 0xE2, 0xD8, 0xE2, 0x9A, 0xE2, 0x89, 0xE2, 0xDD, 0xE2, 0x37,
    0xE2, 0x2F, 0xEA, 0x04, 0xEA, 0x54, 0xEA, 0x4C, 0xEA, 0xD2, 0xEA, 0x8A,
    0xEA, 0x8E, 0xEA, 0xB1, 0xEA, 0xFD, 0xEA, 0x53, 0xEA, 0xAB, 0xEA, 0x47,
    0xEA, 0xCF, 0xEA, 0xFF, 0xE6, 0x10, 0xE6, 0x48, 0xE6, 0x3C, 0xE6, 0x62,
    0xE6, 0x8A, 0xE6, 0xBA, 0xE6, 0x76, 0xE6, 0x5E, 0xE6, 0xC1, 0xE6, 0xB1,
    0xE6, 0xCB, 0xEE, 0xC8, 0xEE, 0x98, 0xEE, 0xF8, 0xEE, 0xC2, 0xEE, 0x1E,
    0xEE, 0x7E, 0xEE, 0x2D, 0xEE, 0x6D, 0xEE, 0x1D, 0xEE, 0x5D, 0xEE, 0x3D,
    0x18, 0x2B, 0x15, 0xC0, 0x39, 0x24, 0x43, 0xE2, 0x1F, 0x00, 0x18, 0x23,
    0x24, 0xC0, 0x28, 0x23, 0x62, 0xC6, 0x1D, 0xA5, 0x03, 0x20, 0x66, 0x52,
    0x0C, 0x95, 0x03, 0x00, 0x19, 0x2C, 0x0C, 0x80, 0x31, 0x12, 0x62, 0xA7,
    0x1C, 0x00, 0x18, 0x2C, 0x0C, 0xC0, 0x29, 0x94, 0xE0, 0x64, 0x9C, 0x85,
    0x02, 0x38, 0x85, 0x12, 0x9C, 0x8C, 0x03, 0x00, 0x10, 0x35, 0xE7, 0x55,
    0xAD, 0x6D, 0x7F, 0x26, 0x91, 0x85, 0xD4, 0x3C, 0xAB, 0xD6, 0xCF, 0x99,
    0x7A, 0x00, 0x10, 0x34, 0x6F, 0xA1, 0x86, 0xCF, 0x3E, 0xAB, 0x0D, 0xBB,
    0x86, 0x7C, 0x6C, 0xB5, 0x6D, 0xCF, 0x24, 0xB2, 0x88, 0x9E, 0xA7, 0x16,
    0xF3, 0xA9, 0xD2, 0xE6, 0x3D, 0xD5, 0x55, 0xFD, 0x01, 0x00, 0x10, 0x32,
    0x74, 0x98, 0xA9, 0xB7, 0x81, 0x1E, 0xA9, 0x87, 0xF4, 0x66, 0xA3, 0xFC,
    0x8B, 0xD2, 0x96, 0x94, 0xFB, 0xFF, 0x10, 0x03, 0x80, 0x8E, 0x16, 0x0D,
    0x00, 0x10, 0x32, 0x7C, 0x90, 0xAB, 0xB7, 0x81, 0x1E, 0xA9, 0xA7, 0x6E,
    0xF7, 0x22, 0xDD, 0xC7, 0xAA, 0xFE, 0xA5, 0x9C, 0xDE, 0xCC, 0x7E, 0xF4,
    0x2E, 0xAC, 0xFA, 0xC7, 0xD9, 0x91, 0xA5, 0xA5, 0xE4, 0xDC, 0x5F, 0xF4,
    0x2B, 0x9D, 0xFC, 0x03, 0x00, 0x10, 0x31, 0x8F, 0xDC, 0xFF, 0x8C, 0x7C,
    0x97, 0xF6, 0x41, 0xE6, 0xE3, 0xF4, 0xF4, 0xF6, 0x47, 0x23, 0xC2, 0x84,
    0xB6, 0x85, 0x74, 0xFF, 0xD0, 0xDD, 0xCF, 0xEE, 0x3F, 0xB7, 0xEB, 0x01,
    0x00, 0x74, 0x7B, 0xA3, 0xDC, 0x2D, 0x3A, 0x5A, 0xB7, 0x56, 0xEE, 0x45,
    0xDF, 0x5B, 0xDA, 0xBF, 0x68, 0xE9, 0x3B, 0xFD, 0x1F, 0xF5, 0x78, 0x27,
    0xFF, 0xA2, 0x4E, 0xF2, 0xDC, 0x1F, 0x00, 0x10, 0x36, 0x76, 0x9B, 0xA9,
    0xB7, 0xBD, 0x1A, 0x1F, 0x66, 0xD4, 0x85, 0xA3, 0xBB, 0xCB, 0x95, 0x83,
    0x00, 0x10, 0x32, 0x6E, 0xDA, 0x27, 0xBB, 0x7D, 0x22, 0x1F, 0xC6, 0x94,
    0x16, 0x9C, 0xDE, 0x97, 0xD6, 0xA5, 0xD3, 0x7F, 0x52, 0x72, 0x58, 0xF2,
    0x4F, 0xD7, 0x85, 0x03, 0x00, 0x10, 0x32, 0x35, 0x96, 0xA9, 0xB9, 0xBD,
    0x1A, 0x1F, 0x86, 0xCE, 0x6E, 0x13, 0x3D, 0x09, 0xE9, 0xF6, 0x00, 0x10,
    0x32, 0x7B, 0x94, 0xAB, 0xB7, 0x81, 0x1E, 0xA9, 0x87, 0x6E, 0xAF, 0x1B,
    0xDD, 0xF9, 0xAA, 0xFE, 0xA4, 0x57, 0xE6, 0xCC, 0x5E, 0xF4, 0x36, 0xAD,
    0xFA, 0xC7, 0xD5, 0xB5, 0xA4, 0xA5, 0xED, 0xDC, 0x5F, 0xF4, 0x73, 0x9E,
    0xFC, 0x03, 0x00, 0x10, 0x32, 0xF7, 0x9F, 0xA9, 0xBD, 0x3F, 0x22, 0x11,
    0x86, 0x6E, 0xCF, 0xA3, 0xDB, 0xFB, 0x46, 0xEB, 0xC8, 0xE9, 0x3F, 0x00,
    0x10, 0x32, 0xAC, 0x98, 0x27, 0xBD, 0x81, 0x22, 0x1F, 0x87, 0xAE, 0x7E,
    0x1C, 0x6D, 0x81, 0xE7, 0xFF, 0x72, 0xE4, 0x20, 0x00, 0xF1, 0xE1, 0x00,
    0x00, 0x11, 0xFC, 0x13, 0xFF, 0x13, 0xFF, 0x00, 0xFE, 0x13, 0xFF, 0x00,
    0x11, 0xFF, 0x00, 0xFF, 0x00, 0xF7, 0x00, 0x18, 0x32, 0xDD, 0xA0, 0x7D,
    0x81, 0x0F, 0xC7, 0x03, 0xE3, 0xEA, 0x53, 0xC6, 0x75, 0xAB, 0xF0, 0x41,
    0xE8, 0x9E, 0x17, 0x73, 0xA1, 0xD2, 0xDC, 0x62, 0xF6, 0x14, 0x34, 0x4D,
    0x0F, 0x8C, 0xB7, 0x54, 0x99, 0x5A, 0xCB, 0x5F, 0x80, 0x84, 0x6D, 0x88,
    0xF3, 0x65, 0x2A, 0x73, 0xBD, 0xF5, 0x77, 0x50, 0xAD, 0x5D, 0xEF, 0xA1,
    0x5A, 0xF5, 0x45, 0x3C, 0x80, 0x53, 0x14, 0x83, 0xC8, 0xBC, 0xC9, 0x05,
    0x60, 0x09, 0x03, 0x68, 0xB0, 0xAF, 0xA9, 0x81, 0x00, 0x38, 0x78, 0xD8,
    0x8F, 0xD9, 0x61, 0xA2, 0x35, 0x77, 0x90, 0x7F, 0x07, 0xD3, 0xDA, 0x80,
    0xFF, 0xEC, 0xB4, 0x66, 0xDF, 0x31, 0xD8, 0xD8, 0x89, 0xBF, 0x65, 0x9B,
    0x9D, 0x5E, 0x82, 0x3E, 0x12, 0x24, 0x21, 0x6F, 0xFC, 0x24, 0x83, 0x03,
    0x00, 0xF2, 0xF3, 0x1F, 0x5C, 0x3E, 0x48, 0x90, 0x60, 0x0D, 0xEE, 0x03,
    0xA5, 0x8B, 0x00, 0x00, 0x1A, 0xFD, 0x38, 0x50, 0xA6, 0x00, 0xF0, 0x03,
    0x21, 0x6E, 0xC7, 0x8D, 0xD9, 0xF3, 0xA0, 0x30, 0xD2, 0x6F, 0x22, 0xF1,
    0x1A, 0x95, 0x71, 0x89, 0x0C, 0x44, 0x8A, 0xC6, 0xA7, 0xD1, 0x6B, 0xA2,
    0x33, 0xAF, 0x9A, 0x41, 0xD1, 0xCE, 0xFC, 0x2E, 0x3B, 0x4D, 0x74, 0xC6,
    0x24, 0x13, 0x18, 0x91, 0x61, 0x9E, 0x94, 0xD7, 0x75, 0xCE, 0xD4, 0x53,
    0x0A, 0x24, 0x2A, 0xDB, 0x8F, 0xF2, 0x34, 0xD0, 0x19, 0x5B, 0x6A, 0x80,
    0x64, 0x47, 0x79, 0xD7, 0x2D, 0xF7, 0x39, 0x53, 0x4B, 0x09, 0x90, 0xC8,
    0x68, 0x1F, 0xAB, 0xBD, 0x46, 0x69, 0xDA, 0x26, 0x85, 0x08, 0xA2, 0xFE,
    0x71, 0xF1, 0x55, 0xA9, 0xA4, 0x74, 0xE0, 0x87, 0x0F, 0x1E, 0x65, 0xCC,
    0xDC, 0x48, 0x06, 0x2C, 0x2A, 0xF3, 0xDB, 0xE6, 0xB8, 0x52, 0x9A, 0x7D,
    0xA8, 0xA0, 0x46, 0x85, 0x7E, 0x97, 0x0D, 0x47, 0x3A, 0x63, 0xFB, 0xD4,
    0x2B, 0xB0, 0x28, 0xBE, 0x50, 0xC2, 0x44, 0x67, 0xDE, 0xA1, 0x88, 0x16,
    0x19, 0xE6, 0x53, 0x39, 0x96, 0x28, 0x3F, 0x86, 0x49, 0x05, 0x80, 0xC7,
    0x06, 0x10, 0x49, 0x27, 0x71, 0x00, 0x10, 0xC9, 0xF8, 0x46, 0xDB, 0x33,
    0x5F, 0x51, 0xFB, 0x00, 0x0B, 0xCE, 0x76, 0x9F, 0x68, 0x36, 0xA6, 0x0D,
    0xB2, 0x67, 0xA8, 0x59, 0x19, 0xA6, 0x0A, 0xD8, 0x57, 0x2A, 0x30, 0x84,
    0x24, 0xE0, 0x22, 0x32, 0x8D, 0x6B, 0xB4, 0xCF, 0x60, 0xB3, 0xF4, 0xDF,
    0xDF, 0x82, 0xC5, 0xA0, 0x69, 0x91, 0x0C, 0x7A, 0x76, 0xAC, 0x1F, 0xC9,
    0x42, 0xAD, 0x32, 0xAF, 0x98, 0x41, 0x8B, 0x8A, 0xF5, 0x37, 0x59, 0x8A,
    0x75, 0xC6, 0xDE, 0x63, 0xC8, 0xD8, 0xC9, 0x1E, 0x57, 0xC3, 0x91, 0xCE,
    0xB8, 0x88, 0xEE, 0x15, 0x22, 0x8B, 0x13, 0x0E, 0xB3, 0xD0, 0x7D, 0x68,
    0x03, 0xF3, 0xFB, 0x18, 0x23, 0x1C, 0x00, 0x29, 0x18, 0x80, 0x2A, 0xB9,
    0xA6, 0x2E, 0x22, 0x20, 0xD9, 0xC1, 0x1D, 0x36, 0x63, 0x99, 0xCE, 0xD4,
    0x46, 0x04, 0x22, 0x33, 0xBA, 0xC7, 0x6A, 0xB6, 0xCE, 0xC9, 0xEF, 0xD7,
    0x0B, 0x24, 0x58, 0x44, 0xA7, 0xA1, 0x9D, 0xFA, 0x4D, 0x44, 0x12, 0x47,
    0x20, 0x5D, 0x9C, 0x32, 0x2F, 0x54, 0xC9, 0x0A, 0x13, 0xFA, 0x27, 0x3C,
    0xE9, 0x34, 0xE4, 0x02, 0xB0, 0x26, 0x52, 0x40, 0x98, 0x93, 0x58, 0x00,
    0xC5, 0x64, 0x8E, 0x86, 0x7B, 0x91, 0x07, 0x00, 0x93, 0x38, 0xD0, 0xF1,
    0x1F, 0xE2, 0x01, 0x58, 0xF3, 0x39, 0x70, 0x9E, 0x6B, 0xEC, 0x9E, 0x80,
    0x92, 0x1D, 0xFE, 0x6D, 0xF5, 0x9C, 0x67, 0x65, 0x09, 0xE0, 0x00, 0x00,
    0x00, 0xF1, 0xD0, 0xDC, 0x3C, 0x06, 0x1C, 0x4C, 0x6E, 0x07, 0xFC, 0xB1,
    0x54, 0x9A, 0xDA, 0xA7, 0x60, 0x41, 0xA4, 0xEB, 0x7D, 0xA1, 0x95, 0x2A,
    0xC3, 0x16, 0x11, 0x14, 0xD0, 0x6C, 0x0D, 0x1F, 0xA6, 0x50, 0x6B, 0x38,
    0x27, 0x82, 0x82, 0x99, 0x9D, 0xFF, 0xC7, 0x1C, 0xA3, 0x4C, 0x97, 0x34,
    0x50, 0x53, 0x95, 0x00, 0xAA, 0xE6, 0x91, 0x2D, 0x19, 0x00, 0x10, 0xF2,
    0x04, 0x2F, 0xDB, 0xD0, 0x06, 0xF1, 0x00, 0x10, 0x33, 0x66, 0xA6, 0x67,
    0x79, 0x85, 0x22, 0xA9, 0x87, 0xE6, 0x55, 0xB5, 0x6E, 0x00, 0x50, 0x24,
    0xF5, 0xCC, 0xBC, 0x67, 0x9E, 0xED, 0x0D, 0x8A, 0xA4, 0x9E, 0x51, 0x9B,
    0x6B, 0xF6, 0x5F, 0xBA, 0x97, 0xD1, 0xEE, 0x45, 0xCF, 0xBF, 0xB9, 0x3B,
    0x04, 0x8D, 0x39, 0xF9, 0xF9, 0x7C, 0xAE, 0x48, 0xEA, 0x11, 0x7D, 0x7B,
    0x69, 0xEE, 0xA5, 0xA6, 0x31, 0xBD, 0x3F, 0x1E, 0x00, 0x10, 0x33, 0x56,
    0x22, 0x47, 0x4D, 0x81, 0xAE, 0x92, 0x58, 0xC6, 0x85, 0x53, 0x68, 0xD1,
    0x6F, 0x95, 0xEE, 0xD7, 0xD8, 0x67, 0x1C, 0x35, 0xF4, 0xCE, 0x12, 0xF2,
    0x9A, 0xFB, 0x8D, 0xD8, 0x98, 0x20, 0x11, 0x86, 0x22, 0x7A, 0x3F, 0x5E,
    0xFD, 0x47, 0x5B, 0x57, 0xBB, 0xFF, 0x28, 0x4B, 0x6B, 0xF9, 0x1F, 0x2D,
    0x8F, 0xED, 0xFE, 0xF1, 0x00, 0xD0, 0x56, 0x10, 0x33, 0xEE, 0xD4, 0xE5,
    0xF9, 0xBF, 0x23, 0x2D, 0x67, 0xB4, 0xD5, 0x92, 0xDB, 0x97, 0xB6, 0x68,
    0x52, 0xFB, 0xD1, 0xF2, 0x4F, 0x62, 0x4F, 0xFA, 0x71, 0xCA, 0xEB, 0x47,
    0x39, 0x5F, 0x69, 0xFD, 0xE8, 0x83, 0x2D, 0xAB, 0x8F, 0x07, 0x00, 0xD0,
    0x3E, 0x18, 0x33, 0xED, 0x5E, 0xF9, 0x82, 0x8A, 0xD2, 0x03, 0x03, 0xEB,
    0x14, 0xC2, 0xA6, 0x5D, 0x33, 0xB5, 0x26, 0xD7, 0xE2, 0xC2, 0x90, 0xD6,
    0x86, 0xB4, 0xFB, 0xD1, 0x96, 0x76, 0xFA, 0x4F, 0x67, 0x3A, 0x63, 0xC8,
    0x90, 0xDA, 0xF6, 0x1E, 0x35, 0xB2, 0x07, 0x90, 0xAF, 0xCC, 0x78, 0x00,
    0xD0, 0x61, 0xD0, 0x19, 0xD0, 0x55, 0xF1, 0x00, 0xD0, 0x61, 0x10, 0x37,
    0x76, 0x99, 0xAD, 0xB3, 0x7F, 0x1E, 0xA2, 0xA7, 0x74, 0x8F, 0xB3, 0x1A,
    0xCC, 0xED, 0x8D, 0xA4, 0x37, 0xA8, 0xDD, 0x9F, 0xEE, 0x9E, 0x1D, 0x75,
    0x71, 0x29, 0xF7, 0xA2, 0x66, 0x30, 0xDD, 0x7E, 0xE5, 0x00, 0x98, 0x23,
    0xC2, 0xC7, 0x03, 0x00, 0xD0, 0x06, 0xD0, 0x06, 0xD0, 0x53, 0xD0, 0x06,
    0xF1, 0x00, 0xD0, 0x06, 0xD0, 0x06, 0xD0, 0xA7, 0xF1, 0x00, 0x10, 0x32,
    0xF6, 0x9F, 0xA9, 0xBD, 0x3F, 0x22, 0x11, 0x86, 0x6E, 0xCF, 0xA3, 0xBB,
    0xFB, 0x46, 0xEB, 0xC8, 0xE9, 0xFF, 0x3D, 0xB4, 0x15, 0xF1, 0x00, 0xD8,
    0xB0, 0xD8, 0xB4, 0xF1, 0x00, 0xD0, 0x56, 0x10, 0x34, 0x76, 0x9B, 0xAB,
    0xB9, 0xBD, 0x15, 0x1F, 0x87, 0xEE, 0xC6, 0x1B, 0xB5, 0x3B, 0xEB, 0xFE,
    0xA3, 0xA5, 0xED, 0xDC, 0x9F, 0x8E, 0xBC, 0x9D, 0xEB, 0x96, 0xE3, 0x01,
    0x00, 0x10, 0x32, 0x6D, 0xA0, 0xA7, 0xBF, 0x81, 0x15, 0x1F, 0xCA, 0xB4,
    0xB6, 0x9B, 0x1E, 0x88, 0x96, 0x7D, 0x53, 0xFF, 0xD3, 0x77, 0x8E, 0x6A,
    0x00, 0x7D, 0x0A, 0xF1, 0x00, 0xD0, 0x56, 0x10, 0x32, 0x9C, 0xA0, 0xA9,
    0x2D, 0xBF, 0x22, 0x1F, 0x68, 0xF4, 0xF4, 0xA3, 0xF8, 0x93, 0xDE, 0x80,
    0x55, 0x7F, 0xD3, 0xDA, 0xAF, 0xE6, 0x4F, 0x4A, 0x03, 0x56, 0x1C, 0x4A,
    0xCD, 0x3C, 0x7A, 0x43, 0x9C, 0x99, 0x77, 0x4A, 0xF9, 0xCD, 0x0B, 0x4A,
    0x06, 0x00, 0x53, 0x26, 0x78, 0x3C, 0x00, 0xD0, 0x3E, 0xD8, 0xD2, 0xFE,
    0xD0, 0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD0, 0x61, 0xD0, 0x55, 0xF3, 0xD0,
    0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD0, 0x61, 0xD8, 0x9E, 0xD0, 0x61, 0xF5,
    0xD0, 0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD0, 0x06, 0xD0, 0x06, 0xD0, 0x53,
    0xD0, 0x06, 0xD0, 0x06, 0xF4, 0xD0, 0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD0,
    0x06, 0xD0, 0x06, 0xD8, 0xD1, 0xD0, 0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD8,
    0xCD, 0xFE, 0xD0, 0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD8, 0xB0, 0xD8, 0xB4,
    0xD0, 0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD0, 0x56, 0x10, 0x32, 0x6D, 0x93,
    0xAB, 0xB1, 0xBF, 0x1A, 0x1F, 0x46, 0xEE, 0xED, 0x1A, 0xAD, 0xC7, 0x6A,
    0xF6, 0xA2, 0x35, 0x5B, 0xDD, 0x9F, 0xF4, 0xA4, 0x9B, 0xFC, 0xDB, 0x8B,
    0x3C, 0x00, 0x87, 0x60, 0xF6, 0x7A, 0x68, 0x2B, 0xD8, 0x13, 0xF1, 0x00,
    0xD0, 0x3E, 0xD8, 0xD2, 0xD0, 0x56, 0xD8, 0x13, 0xF1, 0x00, 0xD0, 0x61,
    0xD0, 0x55, 0xF3, 0xD0, 0x56, 0xD8, 0x13, 0xF1, 0x00, 0xD0, 0x61, 0xD8,
    0x9E, 0xD0, 0x61, 0xD0, 0x56, 0xD8, 0x13, 0xF1, 0x00, 0xD0, 0x06, 0xD0,
    0x06, 0xD0, 0x53, 0xD0, 0x06, 0xD0, 0x06, 0xF4, 0xD0, 0x56, 0xD8, 0x13,
    0xF1, 0x00, 0xD0, 0x06, 0xD0, 0x06, 0xD8, 0xD1, 0xD0, 0x56, 0xD8, 0x13,
    0xF1, 0x00, 0xD8, 0xCD, 0xF7, 0xD0, 0x56, 0xD8, 0x13, 0xF1, 0x00, 0xD8,
    0xB0, 0xD8, 0xB4, 0xD0, 0x56, 0xD8, 0x13, 0xF1, 0x00, 0x10, 0x25, 0x02,
    0xC0, 0x10, 0x97, 0xBC, 0xA4, 0x01, 0xA8, 0x02, 0x93, 0xCF, 0xD8, 0x7D,
    0xB6, 0xD6, 0xFE, 0x6A, 0x7C, 0x1C, 0xD2, 0x1D, 0xD0, 0xEE, 0x3F, 0x5A,
    0xFE, 0x4D, 0xFD, 0x47, 0x4B, 0xC6, 0xB9, 0xFF, 0x88, 0x03, 0x20, 0x43,
    0x27, 0x97, 0xE9, 0x40, 0x3D, 0xBD, 0xED, 0xD5, 0xF8, 0x38, 0xA3, 0x2E,
    0x24, 0xDD, 0x5D, 0xF4, 0xCD, 0xA4, 0xDB, 0x8F, 0xBA, 0x95, 0x74, 0xFF,
    0xD1, 0x8E, 0x72, 0xEE, 0x1F, 0x0F, 0x00, 0xD0, 0x3E, 0x10, 0x35, 0x37,
    0x9A, 0xAB, 0xB5, 0xBF, 0x1A, 0x1F, 0xC7, 0x74, 0x4F, 0xB3, 0xFA, 0x97,
    0xBE, 0x7E, 0x15, 0x03, 0x52, 0x33, 0x93, 0x66, 0x60, 0x52, 0x00, 0xAC,
    0xF1, 0x06, 0x4E, 0x1A, 0x80, 0x3B, 0x06, 0xC5, 0x0C, 0xF7, 0xEA, 0x69,
    0xED, 0xAF, 0xC6, 0xC7, 0x21, 0xED, 0x90, 0xE7, 0x06, 0xA2, 0x15, 0xF6,
    0xD4, 0x7F, 0x3E, 0xA4, 0x00, 0x48, 0xE3, 0x91, 0xC7, 0x03, 0x00, 0xD0,
    0x56, 0xD8, 0xBA, 0xF1, 0x00, 0xD0, 0x56, 0xD8, 0x13, 0xF1, 0x00, 0x10,
    0x28, 0x1D, 0xC0, 0x18, 0x1D, 0x7C, 0x86, 0xDC, 0x33, 0xB5, 0x2E, 0x4F,
    0xE3, 0xD2, 0x8C, 0xD6, 0x7F, 0x75, 0xF7, 0x51, 0x1B, 0xB1, 0x6E, 0x3F,
    0x7A, 0xFB, 0xD5, 0xFD, 0xA1, 0x0D, 0x00, 0xD0, 0x06, 0xF1, 0x00, 0x10,
    0x34, 0x76, 0x9C, 0xA9, 0xBB, 0x7F, 0x1D, 0x22, 0x68, 0x74, 0x7F, 0xAB,
    0xFC, 0x8F, 0xB2, 0x77, 0x73, 0xFF, 0x99, 0xCB, 0x30, 0x62, 0xC7, 0x5F,
    0x53, 0x82, 0x9E, 0x4F, 0xE2, 0x01, 0x58, 0xF2, 0xF1, 0x67, 0x4C, 0x44,
    0x53, 0x6F, 0xFB, 0x3A, 0x44, 0x90, 0xA8, 0xE9, 0x4B, 0x77, 0x97, 0x2B,
    0xD1, 0xE3, 0x01, 0x00, 0xD0, 0x19, 0xD0, 0x55, 0xF1, 0x00, 0x10, 0x32,
    0xB4, 0xA9, 0xA9, 0xBB, 0x7F, 0x1D, 0x22, 0x48, 0xEE, 0x96, 0x0D, 0xDD,
    0x8F, 0x6B, 0xFF, 0x72, 0xBB, 0x73, 0xE8, 0x1E, 0x6D, 0xF9, 0x17, 0x7D,
    0x69, 0xEB, 0xFE, 0xA1, 0x2C, 0xE3, 0xDC, 0x60, 0xF4, 0xB4, 0x9B, 0x1A,
    0xC4, 0x9D, 0x69, 0x73, 0x56, 0x9B, 0xA8, 0x4B, 0x45, 0x37, 0x88, 0x63,
    0xAB, 0xE2, 0x01, 0x00, 0xF1, 0x00, 0xF1, 0x00, 0xF1, 0x00, 0xF1, 0x00,
    0xF1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* ======================================================================== */
/*  SP0256_DATAFMT   -- Data format table for the SP0256's microsequencer   */
/*                                                                          */
/*  len     4 bits      Length of field to extract                          */
/*  lshift  4 bits      Left-shift amount on field                          */
/*  param   4 bits      Parameter number being updated                      */
/*  delta   1 bit       This is a delta-update.  (Implies sign-extend)      */
/*  field   1 bit       This is a field replace.                            */
/*  clr5    1 bit       Clear F5, B5.                                       */
/*  clrall  1 bit       Clear all before doing this update                  */
/* ======================================================================== */

#define CR(l,s,p,d,f,c5,ca)         \
        (                           \
            (((l)  & 15) <<  0) |   \
            (((s)  & 15) <<  4) |   \
            (((p)  & 15) <<  8) |   \
            (((d)  &  1) << 12) |   \
            (((f)  &  1) << 13) |   \
            (((c5) &  1) << 14) |   \
            (((ca) &  1) << 15)     \
        )

#define CR_DELTA  CR(0,0,0,1,0,0,0)
#define CR_FIELD  CR(0,0,0,0,1,0,0)
#define CR_CLR5   CR(0,0,0,0,0,1,0)
#define CR_CLRL   CR(0,0,0,0,0,0,1)
#define CR_LEN(x) ((x) & 15)
#define CR_SHF(x) (((x) >> 4) & 15)
#define CR_PRM(x) (((x) >> 8) & 15)

enum { AM = 0, PR, B0, F0, B1, F1, B2, F2, B3, F3, B4, F4, B5, F5, IA, IP };

static const uint16_t sp0256_datafmt[] =
{
    /* -------------------------------------------------------------------- */
    /*  OPCODE 1111: PAUSE                                                  */
    /* -------------------------------------------------------------------- */
    /*    0 */  CR( 0,  0,  0,  0,  0,  0,  0),     /*  Clear all   */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0001: LOADALL                                                */
    /* -------------------------------------------------------------------- */
                /* Mode modes x1    */
    /*    1 */  CR( 8,  0,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*    2 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*    3 */  CR( 8,  0,  B0, 0,  0,  0,  0),     /*  B0          */
    /*    4 */  CR( 8,  0,  F0, 0,  0,  0,  0),     /*  F0          */
    /*    5 */  CR( 8,  0,  B1, 0,  0,  0,  0),     /*  B1          */
    /*    6 */  CR( 8,  0,  F1, 0,  0,  0,  0),     /*  F1          */
    /*    7 */  CR( 8,  0,  B2, 0,  0,  0,  0),     /*  B2          */
    /*    8 */  CR( 8,  0,  F2, 0,  0,  0,  0),     /*  F2          */
    /*    9 */  CR( 8,  0,  B3, 0,  0,  0,  0),     /*  B3          */
    /*   10 */  CR( 8,  0,  F3, 0,  0,  0,  0),     /*  F3          */
    /*   11 */  CR( 8,  0,  B4, 0,  0,  0,  0),     /*  B4          */
    /*   12 */  CR( 8,  0,  F4, 0,  0,  0,  0),     /*  F4          */
    /*   13 */  CR( 8,  0,  B5, 0,  0,  0,  0),     /*  B5          */
    /*   14 */  CR( 8,  0,  F5, 0,  0,  0,  0),     /*  F5          */
    /*   15 */  CR( 8,  0,  IA, 0,  0,  0,  0),     /*  Amp Interp  */
    /*   16 */  CR( 8,  0,  IP, 0,  0,  0,  0),     /*  Pit Interp  */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0100: LOAD_4                                                 */
    /* -------------------------------------------------------------------- */
                /* Mode 00 and 01           */
    /*   17 */  CR( 6,  2,  AM, 0,  0,  0,  1),     /*  Amplitude   */
    /*   18 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*   19 */  CR( 4,  3,  B3, 0,  0,  0,  0),     /*  B3 (S=0)    */
    /*   20 */  CR( 6,  2,  F3, 0,  0,  0,  0),     /*  F3          */
    /*   21 */  CR( 7,  1,  B4, 0,  0,  0,  0),     /*  B4          */
    /*   22 */  CR( 6,  2,  F4, 0,  0,  0,  0),     /*  F4          */
                /* Mode 01 only             */
    /*   23 */  CR( 8,  0,  B5, 0,  0,  0,  0),     /*  B5          */
    /*   24 */  CR( 8,  0,  F5, 0,  0,  0,  0),     /*  F5          */

                /* Mode 10 and 11           */
    /*   25 */  CR( 6,  2,  AM, 0,  0,  0,  1),     /*  Amplitude   */
    /*   26 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*   27 */  CR( 6,  1,  B3, 0,  0,  0,  0),     /*  B3 (S=0)    */
    /*   28 */  CR( 7,  1,  F3, 0,  0,  0,  0),     /*  F3          */
    /*   29 */  CR( 8,  0,  B4, 0,  0,  0,  0),     /*  B4          */
    /*   30 */  CR( 8,  0,  F4, 0,  0,  0,  0),     /*  F4          */
                /* Mode 11 only             */
    /*   31 */  CR( 8,  0,  B5, 0,  0,  0,  0),     /*  B5          */
    /*   32 */  CR( 8,  0,  F5, 0,  0,  0,  0),     /*  F5          */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0110: SETMSB_6                                               */
    /* -------------------------------------------------------------------- */
                /* Mode 00 only             */
    /*   33 */  CR( 0,  0,  0,  0,  0,  0,  0),
                /* Mode 00 and 01           */
    /*   34 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*   35 */  CR( 6,  2,  F3, 0,  1,  0,  0),     /*  F3 (5 MSBs) */
    /*   36 */  CR( 6,  2,  F4, 0,  1,  0,  0),     /*  F4 (5 MSBs) */
                /* Mode 01 only             */
    /*   37 */  CR( 8,  0,  F5, 0,  1,  0,  0),     /*  F5 (5 MSBs) */

                /* Mode 10 only             */
    /*   38 */  CR( 0,  0,  0,  0,  0,  0,  0),
                /* Mode 10 and 11           */
    /*   39 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*   40 */  CR( 7,  1,  F3, 0,  1,  0,  0),     /*  F3 (6 MSBs) */
    /*   41 */  CR( 8,  0,  F4, 0,  1,  0,  0),     /*  F4 (6 MSBs) */
                /* Mode 11 only             */
    /*   42 */  CR( 8,  0,  F5, 0,  1,  0,  0),     /*  F5 (6 MSBs) */

    /*   43 */  0,
    /*   44 */  0,

    /* -------------------------------------------------------------------- */
    /*  Opcode 1001: DELTA_9                                                */
    /* -------------------------------------------------------------------- */
                /* Mode 00 and 01           */
    /*   45 */  CR( 4,  2,  AM, 1,  0,  0,  0),     /*  Amplitude   */
    /*   46 */  CR( 5,  0,  PR, 1,  0,  0,  0),     /*  Period      */
    /*   47 */  CR( 3,  4,  B0, 1,  0,  0,  0),     /*  B0 4 MSBs   */
    /*   48 */  CR( 3,  3,  F0, 1,  0,  0,  0),     /*  F0 5 MSBs   */
    /*   49 */  CR( 3,  4,  B1, 1,  0,  0,  0),     /*  B1 4 MSBs   */
    /*   50 */  CR( 3,  3,  F1, 1,  0,  0,  0),     /*  F1 5 MSBs   */
    /*   51 */  CR( 3,  4,  B2, 1,  0,  0,  0),     /*  B2 4 MSBs   */
    /*   52 */  CR( 3,  3,  F2, 1,  0,  0,  0),     /*  F2 5 MSBs   */
    /*   53 */  CR( 3,  3,  B3, 1,  0,  0,  0),     /*  B3 5 MSBs   */
    /*   54 */  CR( 4,  2,  F3, 1,  0,  0,  0),     /*  F3 6 MSBs   */
    /*   55 */  CR( 4,  1,  B4, 1,  0,  0,  0),     /*  B4 7 MSBs   */
    /*   56 */  CR( 4,  2,  F4, 1,  0,  0,  0),     /*  F4 6 MSBs   */
                /* Mode 01 only             */
    /*   57 */  CR( 5,  0,  B5, 1,  0,  0,  0),     /*  B5 8 MSBs   */
    /*   58 */  CR( 5,  0,  F5, 1,  0,  0,  0),     /*  F5 8 MSBs   */

                /* Mode 10 and 11           */
    /*   59 */  CR( 4,  2,  AM, 1,  0,  0,  0),     /*  Amplitude   */
    /*   60 */  CR( 5,  0,  PR, 1,  0,  0,  0),     /*  Period      */
    /*   61 */  CR( 4,  1,  B0, 1,  0,  0,  0),     /*  B0 7 MSBs   */
    /*   62 */  CR( 4,  2,  F0, 1,  0,  0,  0),     /*  F0 6 MSBs   */
    /*   63 */  CR( 4,  1,  B1, 1,  0,  0,  0),     /*  B1 7 MSBs   */
    /*   64 */  CR( 4,  2,  F1, 1,  0,  0,  0),     /*  F1 6 MSBs   */
    /*   65 */  CR( 4,  1,  B2, 1,  0,  0,  0),     /*  B2 7 MSBs   */
    /*   66 */  CR( 4,  2,  F2, 1,  0,  0,  0),     /*  F2 6 MSBs   */
    /*   67 */  CR( 4,  1,  B3, 1,  0,  0,  0),     /*  B3 7 MSBs   */
    /*   68 */  CR( 5,  1,  F3, 1,  0,  0,  0),     /*  F3 7 MSBs   */
    /*   69 */  CR( 5,  0,  B4, 1,  0,  0,  0),     /*  B4 8 MSBs   */
    /*   70 */  CR( 5,  0,  F4, 1,  0,  0,  0),     /*  F4 8 MSBs   */
                /* Mode 11 only             */
    /*   71 */  CR( 5,  0,  B5, 1,  0,  0,  0),     /*  B5 8 MSBs   */
    /*   72 */  CR( 5,  0,  F5, 1,  0,  0,  0),     /*  F5 8 MSBs   */

    /* -------------------------------------------------------------------- */
    /*  Opcode 1010: SETMSB_A                                               */
    /* -------------------------------------------------------------------- */
                /* Mode 00 only             */
    /*   73 */  CR( 0,  0,  0,  0,  0,  0,  0),
                /* Mode 00 and 01           */
    /*   74 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*   75 */  CR( 5,  3,  F0, 0,  1,  0,  0),     /*  F0 (5 MSBs) */
    /*   76 */  CR( 5,  3,  F1, 0,  1,  0,  0),     /*  F1 (5 MSBs) */
    /*   77 */  CR( 5,  3,  F2, 0,  1,  0,  0),     /*  F2 (5 MSBs) */

                /* Mode 10 only             */
    /*   78 */  CR( 0,  0,  0,  0,  0,  0,  0),
                /* Mode 10 and 11           */
    /*   79 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*   80 */  CR( 6,  2,  F0, 0,  1,  0,  0),     /*  F0 (6 MSBs) */
    /*   81 */  CR( 6,  2,  F1, 0,  1,  0,  0),     /*  F1 (6 MSBs) */
    /*   82 */  CR( 6,  2,  F2, 0,  1,  0,  0),     /*  F2 (6 MSBs) */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0010: LOAD_2  Mode 00 and 10                                 */
    /*  Opcode 1100: LOAD_C  Mode 00 and 10                                 */
    /* -------------------------------------------------------------------- */
                /* LOAD_2, LOAD_C  Mode 00  */
    /*   83 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*   84 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*   85 */  CR( 3,  4,  B0, 0,  0,  0,  0),     /*  B0 (S=0)    */
    /*   86 */  CR( 5,  3,  F0, 0,  0,  0,  0),     /*  F0          */
    /*   87 */  CR( 3,  4,  B1, 0,  0,  0,  0),     /*  B1 (S=0)    */
    /*   88 */  CR( 5,  3,  F1, 0,  0,  0,  0),     /*  F1          */
    /*   89 */  CR( 3,  4,  B2, 0,  0,  0,  0),     /*  B2 (S=0)    */
    /*   90 */  CR( 5,  3,  F2, 0,  0,  0,  0),     /*  F2          */
    /*   91 */  CR( 4,  3,  B3, 0,  0,  0,  0),     /*  B3 (S=0)    */
    /*   92 */  CR( 6,  2,  F3, 0,  0,  0,  0),     /*  F3          */
    /*   93 */  CR( 7,  1,  B4, 0,  0,  0,  0),     /*  B4          */
    /*   94 */  CR( 6,  2,  F4, 0,  0,  0,  0),     /*  F4          */
                /* LOAD_2 only              */
    /*   95 */  CR( 5,  0,  IA, 0,  0,  0,  0),     /*  Ampl. Intr. */
    /*   96 */  CR( 5,  0,  IP, 0,  0,  0,  0),     /*  Per. Intr.  */

                /* LOAD_2, LOAD_C  Mode 10  */
    /*   97 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*   98 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*   99 */  CR( 6,  1,  B0, 0,  0,  0,  0),     /*  B0 (S=0)    */
    /*  100 */  CR( 6,  2,  F0, 0,  0,  0,  0),     /*  F0          */
    /*  101 */  CR( 6,  1,  B1, 0,  0,  0,  0),     /*  B1 (S=0)    */
    /*  102 */  CR( 6,  2,  F1, 0,  0,  0,  0),     /*  F1          */
    /*  103 */  CR( 6,  1,  B2, 0,  0,  0,  0),     /*  B2 (S=0)    */
    /*  104 */  CR( 6,  2,  F2, 0,  0,  0,  0),     /*  F2          */
    /*  105 */  CR( 6,  1,  B3, 0,  0,  0,  0),     /*  B3 (S=0)    */
    /*  106 */  CR( 7,  1,  F3, 0,  0,  0,  0),     /*  F3          */
    /*  107 */  CR( 8,  0,  B4, 0,  0,  0,  0),     /*  B4          */
    /*  108 */  CR( 8,  0,  F4, 0,  0,  0,  0),     /*  F4          */
                /* LOAD_2 only              */
    /*  109 */  CR( 5,  0,  IA, 0,  0,  0,  0),     /*  Ampl. Intr. */
    /*  110 */  CR( 5,  0,  IP, 0,  0,  0,  0),     /*  Per. Intr.  */

    /* -------------------------------------------------------------------- */
    /*  OPCODE 1101: DELTA_D                                                */
    /* -------------------------------------------------------------------- */
                /* Mode 00 and 01           */
    /*  111 */  CR( 4,  2,  AM, 1,  0,  0,  1),     /*  Amplitude   */
    /*  112 */  CR( 5,  0,  PR, 1,  0,  0,  0),     /*  Period      */
    /*  113 */  CR( 3,  3,  B3, 1,  0,  0,  0),     /*  B3 5 MSBs   */
    /*  114 */  CR( 4,  2,  F3, 1,  0,  0,  0),     /*  F3 6 MSBs   */
    /*  115 */  CR( 4,  1,  B4, 1,  0,  0,  0),     /*  B4 7 MSBs   */
    /*  116 */  CR( 4,  2,  F4, 1,  0,  0,  0),     /*  F4 6 MSBs   */
                /* Mode 01 only             */
    /*  117 */  CR( 5,  0,  B5, 1,  0,  0,  0),     /*  B5 8 MSBs   */
    /*  118 */  CR( 5,  0,  F5, 1,  0,  0,  0),     /*  F5 8 MSBs   */

                /* Mode 10 and 11           */
    /*  119 */  CR( 4,  2,  AM, 1,  0,  0,  0),     /*  Amplitude   */
    /*  120 */  CR( 5,  0,  PR, 1,  0,  0,  0),     /*  Period      */
    /*  121 */  CR( 4,  1,  B3, 1,  0,  0,  0),     /*  B3 7 MSBs   */
    /*  122 */  CR( 5,  1,  F3, 1,  0,  0,  0),     /*  F3 7 MSBs   */
    /*  123 */  CR( 5,  0,  B4, 1,  0,  0,  0),     /*  B4 8 MSBs   */
    /*  124 */  CR( 5,  0,  F4, 1,  0,  0,  0),     /*  F4 8 MSBs   */
                /* Mode 11 only             */
    /*  125 */  CR( 5,  0,  B5, 1,  0,  0,  0),     /*  B5 8 MSBs   */
    /*  126 */  CR( 5,  0,  F5, 1,  0,  0,  0),     /*  F5 8 MSBs   */

    /* -------------------------------------------------------------------- */
    /*  OPCODE 1110: LOAD_E                                                 */
    /* -------------------------------------------------------------------- */
    /*  127 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*  128 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0010: LOAD_2  Mode 01 and 11                                 */
    /*  Opcode 1100: LOAD_C  Mode 01 and 11                                 */
    /* -------------------------------------------------------------------- */
                /* LOAD_2, LOAD_C  Mode 01  */
    /*  129 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*  130 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*  131 */  CR( 3,  4,  B0, 0,  0,  0,  0),     /*  B0 (S=0)    */
    /*  132 */  CR( 5,  3,  F0, 0,  0,  0,  0),     /*  F0          */
    /*  133 */  CR( 3,  4,  B1, 0,  0,  0,  0),     /*  B1 (S=0)    */
    /*  134 */  CR( 5,  3,  F1, 0,  0,  0,  0),     /*  F1          */
    /*  135 */  CR( 3,  4,  B2, 0,  0,  0,  0),     /*  B2 (S=0)    */
    /*  136 */  CR( 5,  3,  F2, 0,  0,  0,  0),     /*  F2          */
    /*  137 */  CR( 4,  3,  B3, 0,  0,  0,  0),     /*  B3 (S=0)    */
    /*  138 */  CR( 6,  2,  F3, 0,  0,  0,  0),     /*  F3          */
    /*  139 */  CR( 7,  1,  B4, 0,  0,  0,  0),     /*  B4          */
    /*  140 */  CR( 6,  2,  F4, 0,  0,  0,  0),     /*  F4          */
    /*  141 */  CR( 8,  0,  B5, 0,  0,  0,  0),     /*  B5          */
    /*  142 */  CR( 8,  0,  F5, 0,  0,  0,  0),     /*  F5          */
                /* LOAD_2 only              */
    /*  143 */  CR( 5,  0,  IA, 0,  0,  0,  0),     /*  Ampl. Intr. */
    /*  144 */  CR( 5,  0,  IP, 0,  0,  0,  0),     /*  Per. Intr.  */

                /* LOAD_2, LOAD_C  Mode 11  */
    /*  145 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*  146 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*  147 */  CR( 6,  1,  B0, 0,  0,  0,  0),     /*  B0 (S=0)    */
    /*  148 */  CR( 6,  2,  F0, 0,  0,  0,  0),     /*  F0          */
    /*  149 */  CR( 6,  1,  B1, 0,  0,  0,  0),     /*  B1 (S=0)    */
    /*  150 */  CR( 6,  2,  F1, 0,  0,  0,  0),     /*  F1          */
    /*  151 */  CR( 6,  1,  B2, 0,  0,  0,  0),     /*  B2 (S=0)    */
    /*  152 */  CR( 6,  2,  F2, 0,  0,  0,  0),     /*  F2          */
    /*  153 */  CR( 6,  1,  B3, 0,  0,  0,  0),     /*  B3 (S=0)    */
    /*  154 */  CR( 7,  1,  F3, 0,  0,  0,  0),     /*  F3          */
    /*  155 */  CR( 8,  0,  B4, 0,  0,  0,  0),     /*  B4          */
    /*  156 */  CR( 8,  0,  F4, 0,  0,  0,  0),     /*  F4          */
    /*  157 */  CR( 8,  0,  B5, 0,  0,  0,  0),     /*  B5          */
    /*  158 */  CR( 8,  0,  F5, 0,  0,  0,  0),     /*  F5          */
                /* LOAD_2 only              */
    /*  159 */  CR( 5,  0,  IA, 0,  0,  0,  0),     /*  Ampl. Intr. */
    /*  160 */  CR( 5,  0,  IP, 0,  0,  0,  0),     /*  Per. Intr.  */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0011: SETMSB_3                                               */
    /*  Opcode 0101: SETMSB_5                                               */
    /* -------------------------------------------------------------------- */
                /* Mode 00 only             */
    /*  161 */  CR( 0,  0,  0,  0,  0,  0,  0),
                /* Mode 00 and 01           */
    /*  162 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*  163 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*  164 */  CR( 5,  3,  F0, 0,  1,  0,  0),     /*  F0 (5 MSBs) */
    /*  165 */  CR( 5,  3,  F1, 0,  1,  0,  0),     /*  F1 (5 MSBs) */
    /*  166 */  CR( 5,  3,  F2, 0,  1,  0,  0),     /*  F2 (5 MSBs) */
                /* SETMSB_3 only            */
    /*  167 */  CR( 5,  0,  IA, 0,  0,  0,  0),     /*  Ampl. Intr. */
    /*  168 */  CR( 5,  0,  IP, 0,  0,  0,  0),     /*  Per. Intr.  */

                /* Mode 10 only             */
    /*  169 */  CR( 0,  0,  0,  0,  0,  0,  0),
                /* Mode 10 and 11           */
    /*  170 */  CR( 6,  2,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*  171 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*  172 */  CR( 6,  2,  F0, 0,  1,  0,  0),     /*  F0 (6 MSBs) */
    /*  173 */  CR( 6,  2,  F1, 0,  1,  0,  0),     /*  F1 (6 MSBs) */
    /*  174 */  CR( 6,  2,  F2, 0,  1,  0,  0),     /*  F2 (6 MSBs) */
                /* SETMSB_3 only            */
    /*  175 */  CR( 5,  0,  IA, 0,  0,  0,  0),     /*  Ampl. Intr. */
    /*  176 */  CR( 5,  0,  IP, 0,  0,  0,  0),     /*  Per. Intr.  */

    /* -------------------------------------------------------------------- */
    /*  Opcode 0001: LOADALL                                                */
    /* -------------------------------------------------------------------- */
                /* Mode x0    */
    /*  177 */  CR( 8,  0,  AM, 0,  0,  0,  0),     /*  Amplitude   */
    /*  178 */  CR( 8,  0,  PR, 0,  0,  0,  0),     /*  Period      */
    /*  179 */  CR( 8,  0,  B0, 0,  0,  0,  0),     /*  B0          */
    /*  180 */  CR( 8,  0,  F0, 0,  0,  0,  0),     /*  F0          */
    /*  181 */  CR( 8,  0,  B1, 0,  0,  0,  0),     /*  B1          */
    /*  182 */  CR( 8,  0,  F1, 0,  0,  0,  0),     /*  F1          */
    /*  183 */  CR( 8,  0,  B2, 0,  0,  0,  0),     /*  B2          */
    /*  184 */  CR( 8,  0,  F2, 0,  0,  0,  0),     /*  F2          */
    /*  185 */  CR( 8,  0,  B3, 0,  0,  0,  0),     /*  B3          */
    /*  186 */  CR( 8,  0,  F3, 0,  0,  0,  0),     /*  F3          */
    /*  187 */  CR( 8,  0,  B4, 0,  0,  0,  0),     /*  B4          */
    /*  188 */  CR( 8,  0,  F4, 0,  0,  0,  0),     /*  F4          */
    /*  189 */  CR( 8,  0,  IA, 0,  0,  0,  0),     /*  Amp Interp  */
    /*  190 */  CR( 8,  0,  IP, 0,  0,  0,  0),     /*  Pit Interp  */

};



static const int16_t sp0256_df_idx[16 * 8] =
{
    /*  OPCODE 0000 */      -1, -1,     -1, -1,     -1, -1,     -1, -1,
    /*  OPCODE 1000 */      -1, -1,     -1, -1,     -1, -1,     -1, -1,
    /*  OPCODE 0100 */      17, 22,     17, 24,     25, 30,     25, 32,
    /*  OPCODE 1100 */      83, 94,     129,142,    97, 108,    145,158,
    /*  OPCODE 0010 */      83, 96,     129,144,    97, 110,    145,160,
    /*  OPCODE 1010 */      73, 77,     74, 77,     78, 82,     79, 82,
    /*  OPCODE 0110 */      33, 36,     34, 37,     38, 41,     39, 42,
    /*  OPCODE 1110 */      127,128,    127,128,    127,128,    127,128,
    /*  OPCODE 0001 */      177,190,    1,  16,     177,190,    1,  16,
    /*  OPCODE 1001 */      45, 56,     45, 58,     59, 70,     59, 72,
    /*  OPCODE 0101 */      161,166,    162,166,    169,174,    170,174,
    /*  OPCODE 1101 */      111,116,    111,118,    119,124,    119,126,
    /*  OPCODE 0011 */      161,168,    162,168,    169,176,    170,176,
    /*  OPCODE 1011 */      -1, -1,     -1, -1,     -1, -1,     -1, -1,
    /*  OPCODE 0111 */      -1, -1,     -1, -1,     -1, -1,     -1, -1,
    /*  OPCODE 1111 */      0,  0,      0,  0,      0,  0,      0,  0
};

/* ======================================================================== */
/*  SP0256_GETB  -- Get up to 8 bits at the current PC.                     */
/* ======================================================================== */
static uint32_t sp0256_getb(ivoice_t *ivoice, int len)
{
    uint32_t data = 0;
    uint32_t d0, d1;

    /* -------------------------------------------------------------------- */
    /*  Fetch data from the FIFO or from the MASK                           */
    /* -------------------------------------------------------------------- */
    if (ivoice->fifo_sel)
    {
        d0 = ivoice->fifo[(ivoice->fifo_tail    ) & 63];
        d1 = ivoice->fifo[(ivoice->fifo_tail + 1) & 63];

        data = ((d1 << 10) | d0) >> ivoice->fifo_bitp;

#ifdef DEBUG_FIFO
        //dfprintf(("IV: RD_FIFO %.3X %d.%d %d\n", data & ((1u << len) - 1),
        //        ivoice->fifo_tail, ivoice->fifo_bitp, ivoice->fifo_head));
#endif

        /* ---------------------------------------------------------------- */
        /*  Note the PC doesn't advance when we execute from FIFO.          */
        /*  Just the FIFO's bit-pointer advances.   (That's not REALLY      */
        /*  what happens, but that's roughly how it behaves.)               */
        /* ---------------------------------------------------------------- */
        ivoice->fifo_bitp += len;
        if (ivoice->fifo_bitp >= 10)
        {
            ivoice->fifo_tail++;
            ivoice->fifo_bitp -= 10;
        }
    } else
    {
        /* ---------------------------------------------------------------- */
        /*  Figure out which ROMs are being fetched into, and grab two      */
        /*  adjacent bytes.  The byte we're interested in is extracted      */
        /*  from the appropriate bit-boundary between them.                 */
        /* ---------------------------------------------------------------- */
        int idx0 = (ivoice->pc    ) >> 3, page0 = idx0 >> 12;
        int idx1 = (ivoice->pc + 8) >> 3, page1 = idx1 >> 12;

        idx0 &= 0xFFF;
        idx1 &= 0xFFF;

        d0 = d1 = 0;

        if (ivoice->rom[page0]) d0 = ivoice->rom[page0][idx0];
        if (ivoice->rom[page1]) d1 = ivoice->rom[page1][idx1];

        data = ((d1 << 8) | d0) >> (ivoice->pc & 7);

        ivoice->pc += len;
    }

    /* -------------------------------------------------------------------- */
    /*  Mask data to the requested length.                                  */
    /* -------------------------------------------------------------------- */
    data &= ((1u << len) - 1);

    return data;
}

/* ======================================================================== */
/*  SP0256_MICRO -- Emulate the microsequencer in the SP0256.  Executes     */
/*                  instructions either until the repeat count != 0 or      */
/*                  the sequencer gets halted by a RTS to 0.                */
/* ======================================================================== */
static void sp0256_micro(ivoice_t *iv)
{
    uint8_t  immed4;
    uint8_t  opcode;
    uint16_t cr;
    int      ctrl_xfer = 0;
    int      repeat    = 0;
    int      i, idx0, idx1;

    /* -------------------------------------------------------------------- */
    /*  Only execute instructions while the filter is not busy.             */
    /* -------------------------------------------------------------------- */
    while (iv->filt.rpt <= 0 && iv->filt.cnt <= 0)
    {
        /* ---------------------------------------------------------------- */
        /*  If the CPU is halted, see if we have a new command pending      */
        /*  in the Address LoaD buffer.                                     */
        /* ---------------------------------------------------------------- */
        if (iv->halted && !iv->lrq)
        {
            iv->pc       = iv->ald | (0x1000 << 3);
            iv->fifo_sel = 0;
            iv->halted   = 0;
            iv->lrq      = 0x8000;
            iv->ald      = 0;
/*          for (i = 0; i < 16; i++)*/
/*              iv->filt.r[i] = 0;*/
        }

        /* ---------------------------------------------------------------- */
        /*  If we're still halted, do nothing.                              */
        /* ---------------------------------------------------------------- */
        if (iv->halted)
        {
            iv->filt.rpt = 1;
            iv->filt.cnt = 0;
            iv->lrq      = 0x8000;
            iv->ald      = 0;
/*          for (i = 0; i < 16; i++)*/
/*              iv->filt.r[i] = 0;*/
            return;
        }

        /* ---------------------------------------------------------------- */
        /*  Fetch the first 8 bits of the opcode, which are always in the   */
        /*  same approximate format -- immed4 followed by opcode.           */
        /* ---------------------------------------------------------------- */
        immed4 = sp0256_getb(iv, 4);
        opcode = sp0256_getb(iv, 4);
        repeat = 0;
        ctrl_xfer = 0;

        //jzdprintf(("$%.4X.%.1X: OPCODE %d%d%d%d.%d%d\n",
        //        (iv->pc >> 3) - 1, iv->pc & 7,
        //        !!(opcode & 1), !!(opcode & 2),
        //        !!(opcode & 4), !!(opcode & 8),
        //        !!(iv->mode&4), !!(iv->mode&2)));

        /* ---------------------------------------------------------------- */
        /*  Handle the special cases for specific opcodes.                  */
        /* ---------------------------------------------------------------- */
        switch (opcode)
        {
            /* ------------------------------------------------------------ */
            /*  OPCODE 0000:  RTS / SETPAGE                                 */
            /* ------------------------------------------------------------ */
            case 0x0:
            {
                /* -------------------------------------------------------- */
                /*  If immed4 != 0, then this is a SETPAGE instruction.     */
                /* -------------------------------------------------------- */
                if (immed4)     /* SETPAGE */
                {
                    iv->page = __rev(immed4) >> 13;
                } else
                /* -------------------------------------------------------- */
                /*  Otherwise, this is an RTS / HLT.                        */
                /* -------------------------------------------------------- */
                {
                    uint32_t btrg;

                    /* ---------------------------------------------------- */
                    /*  Figure out our branch target.                       */
                    /* ---------------------------------------------------- */
                    btrg = iv->stack;

                    iv->stack = 0;

                    /* ---------------------------------------------------- */
                    /*  If the branch target is zero, this is a HLT.        */
                    /*  Otherwise, it's an RTS, so set the PC.              */
                    /* ---------------------------------------------------- */
                    if (!btrg)
                    {
                        iv->halted = 1;
                        iv->pc     = 0;
                        ctrl_xfer  = 1;
                    } else
                    {
                        iv->pc    = btrg;
                        ctrl_xfer = 1;
                    }
                }

                break;
            }

            /* ------------------------------------------------------------ */
            /*  OPCODE 0111:  JMP          Jump to 12-bit/16-bit Abs Addr   */
            /*  OPCODE 1011:  JSR          Jump to Subroutine               */
            /* ------------------------------------------------------------ */
            case 0xE:
            case 0xD:
            {
                int btrg;

                /* -------------------------------------------------------- */
                /*  Figure out our branch target.                           */
                /* -------------------------------------------------------- */
                btrg = iv->page                           |
                       (__rev(immed4)             >> 17) |
                       (__rev(sp0256_getb(iv, 8)) >> 21);
                ctrl_xfer = 1;

                /* -------------------------------------------------------- */
                /*  If this is a JSR, push our return address on the        */
                /*  stack.  Make sure it's byte aligned.                    */
                /* -------------------------------------------------------- */
                if (opcode == 0xD)
                    iv->stack = (iv->pc + 7) & ~7;

                /* -------------------------------------------------------- */
                /*  Jump to the new location!                               */
                /* -------------------------------------------------------- */
                iv->pc = btrg;
                break;
            }

            /* ------------------------------------------------------------ */
            /*  OPCODE 1000:  SETMODE      Set the Mode and Repeat MSBs     */
            /* ------------------------------------------------------------ */
            case 0x1:
            {
                iv->mode = ((immed4 & 8) >> 2) | (immed4 & 4) |
                           ((immed4 & 3) << 4);
                break;
            }

            /* ------------------------------------------------------------ */
            /*  OPCODE 0001:  LOADALL      Load All Parameters              */
            /*  OPCODE 0010:  LOAD_2       Load Per, Ampl, Coefs, Interp.   */
            /*  OPCODE 0011:  SETMSB_3     Load Pitch, Ampl, MSBs, & Intrp  */
            /*  OPCODE 0100:  LOAD_4       Load Pitch, Ampl, Coeffs         */
            /*  OPCODE 0101:  SETMSB_5     Load Pitch, Ampl, and Coeff MSBs */
            /*  OPCODE 0110:  SETMSB_6     Load Ampl, and Coeff MSBs.       */
            /*  OPCODE 1001:  DELTA_9      Delta update Ampl, Pitch, Coeffs */
            /*  OPCODE 1010:  SETMSB_A     Load Ampl and MSBs of 3 Coeffs   */
            /*  OPCODE 1100:  LOAD_C       Load Pitch, Ampl, Coeffs         */
            /*  OPCODE 1101:  DELTA_D      Delta update Ampl, Pitch, Coeffs */
            /*  OPCODE 1110:  LOAD_E       Load Pitch, Amplitude            */
            /*  OPCODE 1111:  PAUSE        Silent pause                     */
            /* ------------------------------------------------------------ */
            default:
            {
                repeat    = immed4 | (iv->mode & 0x30);
                break;
            }
        }
        if (opcode != 1) iv->mode &= 0xF;

        /* ---------------------------------------------------------------- */
        /*  If this was a control transfer, handle setting "fifo_sel"       */
        /*  and all that ugliness.                                          */
        /* ---------------------------------------------------------------- */
        if (ctrl_xfer)
        {
            //jzdprintf(("jumping to $%.4X.%.1X: ", iv->pc >> 3, iv->pc & 7));

            /* ------------------------------------------------------------ */
            /*  Set our "FIFO Selected" flag based on whether we're going   */
            /*  to the FIFO's address.                                      */
            /* ------------------------------------------------------------ */
            iv->fifo_sel = iv->pc == FIFO_ADDR;

            //jzdprintf(("%s ", iv->fifo_sel ? "FIFO" : "ROM"));

            /* ------------------------------------------------------------ */
            /*  Control transfers to the FIFO cause it to discard the       */
            /*  partial decle that's at the front of the FIFO.              */
            /* ------------------------------------------------------------ */
            if (iv->fifo_sel && iv->fifo_bitp)
            {
                //jzdprintf(("bitp = %d -> Flush", iv->fifo_bitp));

                /* Discard partially-read decle. */
                if (iv->fifo_tail < iv->fifo_head) iv->fifo_tail++;
                iv->fifo_bitp = 0;
            }

            //jzdprintf(("\n"));

            continue;
        }

        /* ---------------------------------------------------------------- */
        /*  Otherwise, if we have a repeat count, then go grab the data     */
        /*  block and feed it to the filter.                                */
        /* ---------------------------------------------------------------- */
        if (!repeat) continue;


        #ifdef SINGLE_STEP
        jzp_printf("NEXT:\n"); jzp_flush();
        {
        char buf[1024];
        fgets(buf,sizeof(buf),stdin); if (opcode != 0xF) repeat <<= 3;
        }
        #endif

        iv->filt.rpt = repeat;
        //jzdprintf(("repeat = %d\n", repeat));

        /* clear delay line on new opcode */
        for (i = 0; i < 6; i++)
             iv->filt.z_data[i][0] = iv->filt.z_data[i][1] = 0;

        i = (opcode << 3) | (iv->mode & 6);
        idx0 = sp0256_df_idx[i++];
        idx1 = sp0256_df_idx[i  ];

        assert(idx0 >= 0 && idx1 >= 0 && idx1 >= idx0);

        /* ---------------------------------------------------------------- */
        /*  If we're in one of the 10-pole modes (x0), clear F5/B5.         */
        /* ---------------------------------------------------------------- */
        if ((iv->mode & 2) == 0)
            iv->filt.r[F5] = iv->filt.r[B5] = 0;


        /* ---------------------------------------------------------------- */
        /*  Step through control words in the description for data block.   */
        /* ---------------------------------------------------------------- */
        for (i = idx0; i <= idx1; i++)
        {
            int len, shf, delta, field, prm, clrL;
            int8_t value;

            /* ------------------------------------------------------------ */
            /*  Get the control word and pull out some important fields.    */
            /* ------------------------------------------------------------ */
            cr = sp0256_datafmt[i];

            len   = CR_LEN(cr);
            shf   = CR_SHF(cr);
            prm   = CR_PRM(cr);
            clrL  = cr & CR_CLRL;
            delta = cr & CR_DELTA;
            field = cr & CR_FIELD;
            value = 0;

            //jzdprintf(("$%.4X.%.1X: len=%2d shf=%2d prm=%2d d=%d f=%d ",
            //         iv->pc >> 3, iv->pc & 7, len, shf, prm, !!delta, !!field));
            /* ------------------------------------------------------------ */
            /*  Clear any registers that were requested to be cleared.      */
            /* ------------------------------------------------------------ */
            if (clrL)
            {
                iv->filt.r[F0] = iv->filt.r[B0] = 0;
                iv->filt.r[F1] = iv->filt.r[B1] = 0;
                iv->filt.r[F2] = iv->filt.r[B2] = 0;
            }

            /* ------------------------------------------------------------ */
            /*  If this entry has a bitfield with it, grab the bitfield.    */
            /* ------------------------------------------------------------ */
            if (len)
            {
                value = sp0256_getb(iv, len);
            }
            else
            {
                //jzdprintf((" (no update)\n"));
                continue;
            }

            /* ------------------------------------------------------------ */
            /*  Sign extend if this is a delta update.                      */
            /* ------------------------------------------------------------ */
            if (delta)  /* Sign extend */
            {
                if (value & (1u << (len - 1))) value |= -(1u << len);
            }

            /* ------------------------------------------------------------ */
            /*  Shift the value to the appropriate precision.               */
            /* ------------------------------------------------------------ */
            if (shf)
                value = value < 0 ? -(-value << shf) : (value << shf);

            //jzdprintf(("v=%.2X (%c%.2X)  ", value & 0xFF,
            //         value & 0x80 ? '-' : '+',
            //         0xFF & (value & 0x80 ? -value : value)));

            iv->silent = 0;

            /* ------------------------------------------------------------ */
            /*  If this is a field-replace, insert the field.               */
            /* ------------------------------------------------------------ */
            if (field)
            {
                //jzdprintf(("--field-> r[%2d] = %.2X -> ", prm, iv->filt.r[prm]));

                iv->filt.r[prm] &= ~(~0u << shf); /* Clear the old bits.    */
                iv->filt.r[prm] |= value;         /* Merge in the new bits. */

                //jzdprintf(("%.2X\n", iv->filt.r[prm]));

                continue;
            }

            /* ------------------------------------------------------------ */
            /*  If this is a delta update, add to the appropriate field.    */
            /* ------------------------------------------------------------ */
            if (delta)
            {
                //jzdprintf(("--delta-> r[%2d] = %.2X -> ", prm, iv->filt.r[prm]));

                iv->filt.r[prm] += value;

                //jzdprintf(("%.2X\n", iv->filt.r[prm]));

                continue;
            }

            /* ------------------------------------------------------------ */
            /*  Otherwise, just write the new value.                        */
            /* ------------------------------------------------------------ */
            iv->filt.r[prm] = value;
            //jzdprintf(("--value-> r[%2d] = %.2X\n", prm, iv->filt.r[prm]));
        }

        /* ---------------------------------------------------------------- */
        /*  Most opcodes clear IA, IP.                                      */
        /* ---------------------------------------------------------------- */
        if (opcode != 0x1 && opcode != 0x2 && opcode != 0x3)
        {
            iv->filt.r[IA] = 0;
            iv->filt.r[IP] = 0;
        }

        /* ---------------------------------------------------------------- */
        /*  Special case:  Set PAUSE's equivalent period.                   */
        /* ---------------------------------------------------------------- */
        if (opcode == 0xF)
        {
            iv->silent     = 1;
            iv->filt.r[AM] = 0;
            iv->filt.r[PR] = PER_PAUSE;
        }

        /* ---------------------------------------------------------------- */
        /*  Now that we've updated the registers, go decode them.           */
        /* ---------------------------------------------------------------- */
        lpc12_regdec(&iv->filt);

        /* ---------------------------------------------------------------- */
        /*  Break out since we now have a repeat count.                     */
        /* ---------------------------------------------------------------- */
        break;
    }
}

/* ======================================================================== */
/*  IVOICE_TK    -- Where the magic happens.  Generate voice data for       */
/*                  our good friend, the Intellivoice.                      */
/* ======================================================================== */
uint32_t ivoice_tk(uint32_t len)
{
    ivoice_t *ivoice = &intellivoice;
    uint64_t until = (ivoice->now + len) * 4;
    int samples, did_samp;//, old_idx;
    int sys_clock = ivoice->pal_mode ? 4000000 : 3579545;
    int clock_per_samp = ivoice->pal_mode ? 400 : 358;

    /* -------------------------------------------------------------------- */
    /*  If the rest of the machine hasn't caught up to us, just return.     */
    /* -------------------------------------------------------------------- */
    if (until <= ivoice->sound_current) {
        ivoice->now += len;
        return 0;
    }

    /* -------------------------------------------------------------------- */
    /*  Iterate the sound engine.                                           */
    /* -------------------------------------------------------------------- */
    while (ivoice->sound_current < until)
    {
        /* ---------------------------------------------------------------- */
        /*  Renormalize our sc_head and sc_tail.                            */
        /* ---------------------------------------------------------------- */
        while (ivoice->sc_head > SCBUF_SIZE && ivoice->sc_tail > SCBUF_SIZE)
        {
            ivoice->sc_head -= SCBUF_SIZE;
            ivoice->sc_tail -= SCBUF_SIZE;
        }

        /* ---------------------------------------------------------------- */
        /*  First, drain as much of our scratch buffer as we can into the   */
        /*  sound buffers.                                                  */
        /* ---------------------------------------------------------------- */
        while (ivoice->sc_tail < ivoice->sc_head)
        {
            int32_t s, ws;

            ws = s = ivoice->scratch[ivoice->sc_tail++ & SCBUF_MASK];

            if (ivoice->skipping < 0.0)
            {
                ivoice->skipping += 1.0;
                continue;
            }

            if (ivoice->time_scale <= 1.0)
            {
                ivoice->sample_frc += ivoice->rate * clock_per_samp /
                                        ivoice->time_scale;
            } else
            {
                ivoice->sample_frc += ivoice->rate * clock_per_samp;
                ivoice->skipping += ivoice->time_scale - 1.0;
            }

            if (ivoice->skipping >= 512.0)
                ivoice->skipping = -ivoice->skipping;

            /* ------------------------------------------------------------ */
            /*  Update the sliding window in down-sample mode               */
            /* ------------------------------------------------------------ */
            if (ivoice->rate < 10000)
            {
                ivoice->wind_sum -= ivoice->window[ivoice->wind_ptr  ];
                ivoice->wind_sum += ivoice->window[ivoice->wind_ptr++] = s;
                if (ivoice->wind_ptr >= ivoice->wind) ivoice->wind_ptr = 0;

                ws = ivoice->wind_sum / ivoice->wind;
            }

            while (ivoice->sample_frc > sys_clock)
            {
                ivoice->sample_frc -= sys_clock;

                /* -------------------------------------------------------- */
                /*  Update the sliding window in up-sample mode             */
                /* -------------------------------------------------------- */
                if (ivoice->rate >= 10000)
                {
                    ivoice->wind_sum -= ivoice->window[ivoice->wind_ptr  ];
                    ivoice->wind_sum += ivoice->window[ivoice->wind_ptr++] = s;
                    if (ivoice->wind_ptr >= ivoice->wind) ivoice->wind_ptr = 0;

                    ws = ivoice->wind_sum / ivoice->wind;
                }

                /* -------------------------------------------------------- */
                /*  Store out the current sample.                           */
                /* -------------------------------------------------------- */
                ivoice->cur_buf[ivoice->cur_len++] = ws;

                /* -------------------------------------------------------- */
                /*  Commit the buffer when it's full.                       */
                /* -------------------------------------------------------- */
                if (ivoice->cur_len >= ivoiceBufferSize)
                {
                    ivoice->cur_len = 0;
                }
            }
        }

        /* ---------------------------------------------------------------- */
        /*  Calculate the number of samples required at ~10kHz.             */
        /*  (Actually, on NTSC this is 3579545 / 358, or 9998.73 Hz).       */
        /* ---------------------------------------------------------------- */
        samples = ((int)(until - ivoice->sound_current + clock_per_samp - 1))
                / clock_per_samp;

        /* ---------------------------------------------------------------- */
        /*  Process the current set of filter coefficients as long as the   */
        /*  repeat count holds up and we have room in our scratch buffer.   */
        /* ---------------------------------------------------------------- */
        did_samp = 0;
        //old_idx  = ivoice->sc_head;
        if (samples > 0) do
        {
            int do_samp;

            /* ------------------------------------------------------------ */
            /*  If our repeat count expired, emulate the microsequencer.    */
            /* ------------------------------------------------------------ */
            if (ivoice->filt.rpt <= 0 && ivoice->filt.cnt <= 0)
                sp0256_micro(ivoice);

            /* ------------------------------------------------------------ */
            /*  Do as many samples as we can.                               */
            /* ------------------------------------------------------------ */
            do_samp = samples - did_samp;
            if (ivoice->sc_head + do_samp - ivoice->sc_tail > SCBUF_SIZE)
                do_samp = ivoice->sc_tail + SCBUF_SIZE - ivoice->sc_head;

            if (do_samp == 0) break;

            if (ivoice->silent &&
                ivoice->filt.rpt <= 0 && ivoice->filt.cnt <= 0)
            {
                int x, y = ivoice->sc_head;

                for (x = 0; x < do_samp; x++)
                    ivoice->scratch[y++ & SCBUF_MASK] = 0;
                ivoice->sc_head += do_samp;
                did_samp        += do_samp;
            } else
            {
                did_samp += lpc12_update(&ivoice->filt, do_samp,
                                         ivoice->scratch, &ivoice->sc_head);
            }

        } while ((ivoice->filt.rpt > 0 || ivoice->filt.cnt > 0) &&
                 samples > did_samp);

        ivoice->sound_current += did_samp * clock_per_samp;
    }

//  if (per->now*4 - ivoice->sound_current > THRESH)
//      ivoice->snd_buf.drop++;
    ivoice->now += len;
    
    return (ivoice->sound_current >> 2) - (ivoice->now - len);
}


/* ======================================================================== */
/*  IVOICE_RD    -- Handle reads from the Intellivoice.                     */
/* ======================================================================== */
uint32_t ivoice_rd(uint32_t addr)
{
    ivoice_t *ivoice = &intellivoice;

    /* -------------------------------------------------------------------- */
    /*  Address 0x80 returns the SP0256 LRQ status on bit 15.               */
    /* -------------------------------------------------------------------- */
    if (addr == 0)
    {
        return ivoice->lrq;
    }

    /* -------------------------------------------------------------------- */
    /*  Address 0x81 returns the SPB640 FIFO full status on bit 15.         */
    /* -------------------------------------------------------------------- */
    if (addr == 1)
    {
        return (ivoice->fifo_head - ivoice->fifo_tail) >= 64 ? 0x8000 : 0;
    }

    /* -------------------------------------------------------------------- */
    /*  Just return 255 for all other addresses in our range.               */
    /* -------------------------------------------------------------------- */
    return 0x00FF;
}

/* ======================================================================== */
/*  IVOICE_WR    -- Handle writes to the Intellivoice.                      */
/* ======================================================================== */
void ivoice_wr(uint32_t addr, uint32_t data)
{
    ivoice_t *ivoice = &intellivoice;

    /* -------------------------------------------------------------------- */
    /*  Ignore writes outside 0x80, 0x81.                                   */
    /* -------------------------------------------------------------------- */
    if (addr > 1) return;

    /* -------------------------------------------------------------------- */
    /*  Address 0x80 is for Address Loads (essentially speech commands).    */
    /* -------------------------------------------------------------------- */
    if (addr == 0)
    {
        /* ---------------------------------------------------------------- */
        /*  Drop writes to the ALD register if we're busy.                  */
        /* ---------------------------------------------------------------- */
        if (!ivoice->lrq)
            return;

        /* ---------------------------------------------------------------- */
        /*  Set LRQ to "busy" and load the 8 LSBs of the data into the ALD  */
        /*  reg.  We take the command address, and multiply by 2 bytes to   */
        /*  get the new PC address.                                         */
        /* ---------------------------------------------------------------- */
        ivoice->lrq = 0;
        ivoice->ald = (0xFF & data) << 4;

        return;
    }

    /* -------------------------------------------------------------------- */
    /*  Address 0x81 is for FIFOing up decles.  The FIFO is 64 decles       */
    /*  long.  The Head pointer points to where we insert new decles and    */
    /*  the Tail pointer is where we pull them from.                        */
    /* -------------------------------------------------------------------- */
    if (addr == 1)
    {
        /* ---------------------------------------------------------------- */
        /*  If Bit 10 is set, just reset the FIFO and SP0256.               */
        /* ---------------------------------------------------------------- */
        if (data & 0x400)
        {
            ivoice->fifo_head = ivoice->fifo_tail = ivoice->fifo_bitp = 0;

            memset(&ivoice->filt, 0, sizeof(ivoice->filt));
            ivoice->halted   = 1;
            ivoice->filt.rpt = -1;
            ivoice->filt.rng = 1;
            ivoice->lrq      = 0x8000;
            ivoice->ald      = 0x0000;
            ivoice->pc       = 0x0000;
            ivoice->stack    = 0x0000;
            ivoice->fifo_sel = 0;
            ivoice->mode     = 0;
            ivoice->page     = 0x1000 << 3;
            ivoice->silent   = 1;
            return;
        }

        /* ---------------------------------------------------------------- */
        /*  If the FIFO is full, drop the data.                             */
        /* ---------------------------------------------------------------- */
        if ((ivoice->fifo_head - ivoice->fifo_tail) >= 64)
        {
            //jzdprintf(("IV: Dropped FIFO write\n"));
            return;
        }

        /* ---------------------------------------------------------------- */
        /*  FIFO up the lower 10 bits of the data.                          */
        /* ---------------------------------------------------------------- */
#ifdef DEBUG_FIFO
        //dfprintf(("IV: WR_FIFO %.3X %d.%d %d\n", data & 0x3FF,
        //        ivoice->fifo_tail, ivoice->fifo_bitp, ivoice->fifo_head));
#endif
        ivoice->fifo[ivoice->fifo_head++ & 63] = data & 0x3FF;

        return;
    }
}

/* ======================================================================== */
/*  IVOICE_RESET -- Resets the Intellivoice                                 */
/* ======================================================================== */
void ivoice_reset(void)
{
    /* -------------------------------------------------------------------- */
    /*  Do a software-style reset of the Intellivoice.                      */
    /* -------------------------------------------------------------------- */
    ivoice_wr(1, 0x400);
}

/* ======================================================================== */
/*  IVOICE_DTOR  -- Destroy an Intellivoice                                 */
/* ======================================================================== */
void ivoice_dtor(void)
{
    /*
     * Minty port:
     * scratch[] and window[] are embedded arrays in ivoice_t, not heap
     * allocations. Freeing them would corrupt memory or crash.
     */
}

void ivoice_frame(void)
{
    ivoice_t *ivoice = &intellivoice;
    int c;
    
    c = ivoice->cur_len - AUDIO_FREQ / 60;
    if (c > 0)
        memmove(ivoiceBuffer, ivoiceBuffer + AUDIO_FREQ / 60, c * sizeof(int16_t));
    else
        c = 0;
    ivoice->cur_len = c;
}

/* ======================================================================== */
/*  IVOICE_INIT  -- Makes a new Intellivoice                                */
/* ======================================================================== */
int ivoice_init
(
    int             pal_mode,   /*  PAL vs. NTSC                            */
    double          time_scale  /*  For --macho                             */
)
{
    ivoice_t *ivoice = &intellivoice;
    int rate;
    int wind;
    
    ivoiceBufferSize = AUDIO_FREQ / 60 * 2;
    rate = AUDIO_FREQ;   /* Sampling rate */
    wind = -1;  /* Sliding window size */
    
    /* -------------------------------------------------------------------- */
    /*  First, lets zero out the structure to be safe.                      */
    /* -------------------------------------------------------------------- */
    memset(ivoice, 0, sizeof(ivoice_t));

    /* -------------------------------------------------------------------- */
    /*  Sanity checks.                                                      */
    /* -------------------------------------------------------------------- */
    if (rate < 10000 || rate > 96000)
    {
#if defined(IVOICE_ENABLE_PRINTF)
        fprintf(stderr, "ivoice:  Sampling rate of %d is invalid.  Must be "
                        "between 10000 and 96000.\n", rate);
#endif
        return -1;
    }

    /* -------------------------------------------------------------------- */
    /*  If wind == -1, calculate a window size based on the ratio of our    */
    /*  sample rate to the device's native rate.                            */
    /* -------------------------------------------------------------------- */
    if (wind == -1)
    {
        if (rate > 10000)   wind = rate / 5000 + 3;
        else                wind = 20000 / rate + 1;

        if (wind < 1) wind = 1;

//        jzp_printf("ivoice:  Automatic sliding-window setting: %d\n", wind);
    }

    /* -------------------------------------------------------------------- */
    /*  Set up the peripheral.                                              */
    /* -------------------------------------------------------------------- */
//    ivoice->periph.dtor      = ivoice_dtor;

    /* -------------------------------------------------------------------- */
    /*  Configure our internal variables.                                   */
    /* -------------------------------------------------------------------- */
    ivoice->rom[1]     = mask;
    ivoice->rate       = rate;
    ivoice->filt.rng   = 1;
    ivoice->wind       = wind;
    ivoice->wind_sum   = 0;
    ivoice->pal_mode   = pal_mode;
    ivoice->time_scale = time_scale;

    /* -------------------------------------------------------------------- */
    /*  Set up our initial working buffer.                                  */
    /* -------------------------------------------------------------------- */
    ivoice->cur_buf = ivoiceBuffer;
    ivoice->cur_len = 0;

    /* -------------------------------------------------------------------- */
    /*  Allocate a scratch buffer for generating 10kHz samples.             */
    /* -------------------------------------------------------------------- */
    ivoice->sc_head = ivoice->sc_tail = 0;

    /* -------------------------------------------------------------------- */
    /*  Set up the microsequencer's initial state.                          */
    /* -------------------------------------------------------------------- */
    ivoice->halted   = 1;
    ivoice->filt.rpt = -1;
    ivoice->lrq      = 0x8000;
    ivoice->page     = 0x1000 << 3;
    ivoice->silent   = 1;

    return 0;
}


/* ======================================================================== */
/*  This program is free software; you can redistribute it and/or modify    */
/*  it under the terms of the GNU General Public License as published by    */
/*  the Free Software Foundation; either version 2 of the License, or       */
/*  (at your option) any later version.                                     */
/*                                                                          */
/*  This program is distributed in the hope that it will be useful,         */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       */
/*  General Public License for more details.                                */
/*                                                                          */
/*  You should have received a copy of the GNU General Public License along */
/*  with this program; if not, write to the Free Software Foundation, Inc., */
/*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.             */
/* ======================================================================== */
/*                 Copyright (c) 1998-2000, Joseph Zbiciak                  */
/* ======================================================================== */
