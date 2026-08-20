/*
 * Minty SP0256 / Intellivoice speech core
 *
 * This file is a derivative work based on the Intellivoice emulation written
 * by Joseph Zbiciak for the original jzIntv project. The implementation has
 * been substantially reduced and specialized for the Minty firmware direct
 * sample path while retaining the original SP0256 decoding model.
 *
 * Operation summary:
 *   - ivoice_init() initializes the single static SP0256 instance and points
 *     the internal ROM page at the embedded speech mask.
 *   - ivoice_wr() accepts Address Load commands and SPB640 FIFO data. A FIFO
 *     write with bit 10 set performs the hardware-style core reset directly.
 *   - ivoice_minty_next_sample() produces exactly one native-rate signed
 *     16-bit PCM sample per call.
 *   - When the current frame expires, sp0256_micro() interprets SP0256
 *     microcode until another frame is loaded or the processor halts.
 *   - Internal decoder and opcode functions access the single global device
 *     instance directly, avoiding repeated state-pointer arguments.
 *   - lpc12_update() generates one excitation value, applies parameter
 *     interpolation when required, runs the six cascaded second-order LPC
 *     stages, limits the result, and returns the resulting sample.
 *   - Time conversion, buffered rendering, host audio mixing, dynamic memory,
 *     destructor handling, and the former multi-sample LPC interface are not
 *     part of this specialized implementation.
 *   - Performance-critical routines use IVOICE_HOT() so the Pico build can
 *     place the active speech path in RAM rather than execute it from flash.
 *
 * Copyright (c) 1998-2000 Joseph Zbiciak for the original jzIntv work and
 * subsequent contributors for this derivative implementation.
 *
 * This derivative remains distributed under the GNU General Public License,
 * version 2 or, at the user's option, any later version. It is provided
 * without warranty, including implied warranties of merchantability or
 * fitness for a particular purpose. See the GNU GPL for complete terms.
 */


#define PER_PAUSE    (64)               /* Equiv timing period for pauses.  */
#define PER_NOISE    (64)               /* Equiv timing period for noise.   */

#define FIFO_ADDR    (0x1800 << 3)      /* SP0256 address of speech FIFO.   */
#define SCBUF_MASK   4095u

enum { AM = 0, PR, B0, F0, B1, F1, B2, F2, B3, F3, B4, F4, B5, F5, IA, IP };

#include <stdint.h>
#include <string.h>
#include "pico/bit_ops.h"
#include "pico/platform.h"
/*
 * Minty port:
 * - remove FreeIntv/libretro dependencies
 */
#include "ivoice.h"

#define INLINE inline
#define IVOICE_HOT(name) __not_in_flash_func(name)

ivoice_t intellivoice;


/* ======================================================================== */
/*  Internal function prototypes.                                           */
/* ======================================================================== */
static INLINE int16_t  IVOICE_HOT(limit)(int16_t s);
static uint32_t        IVOICE_HOT(sp0256_getb)(int len);
static int             IVOICE_HOT(lpc12_update)(lpc12_t *f, int16_t *sample);
static void            IVOICE_HOT(lpc12_regdec)(lpc12_t *f);
static void            IVOICE_HOT(sp0256_micro)(void);

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
static INLINE int16_t IVOICE_HOT(limit)(int16_t s)
{
    if (s >  127) return  127;
    if (s < -128) return -128;
    return s;
}


/* ======================================================================== */
/*  AMP_DECODE       -- Decode amplitude register                           */
/* ======================================================================== */
static int IVOICE_HOT(amp_decode)(uint8_t a)
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

    return ampl;
}

/* ======================================================================== */
/*  LPC12_UPDATE     -- Update the 12-pole filter, outputting samples.      */
/* ======================================================================== */
static int IVOICE_HOT(lpc12_update)(lpc12_t *f, int16_t *sample)
{
    int j;
    int16_t samp = 0;
    int bit = f->rng & 1;
    int do_int = 0;

    f->rng = (f->rng >> 1) ^ (bit ? 0x4001 : 0);

    if (--f->cnt <= 0)
    {
        if (f->rpt-- <= 0)
        {
            f->cnt = f->rpt = 0;
            *sample = 0;
            return 0;
        }

        f->cnt = f->per ? f->per : PER_NOISE;
        samp = f->amp;
        do_int = f->interp;
    }

    if (!f->per)
        samp = bit ? -f->amp : f->amp;

    if (do_int)
    {
        f->r[0] += f->r[14];
        f->r[1] += f->r[15];
        f->amp = amp_decode(f->r[0]);
        f->per = f->r[1];
    }

    for (j = 0; j < 6; j++)
    {
        samp += (((int)f->b_coef[j] * (int)f->z_data[j][1]) >> 9);
        samp += (((int)f->f_coef[j] * (int)f->z_data[j][0]) >> 8);
        f->z_data[j][1] = f->z_data[j][0];
        f->z_data[j][0] = samp;
    }

    *sample = limit(samp >> 4) * 256;
    return 1;
}

/*static int stage_map[6] = { 4, 2, 0, 5, 3, 1 };*/
/*static int stage_map[6] = { 3, 0, 4, 1, 5, 2 };*/
/*static int stage_map[6] = { 3, 0, 1, 4, 2, 5 };*/
static const int stage_map[6] = { 0, 1, 2, 3, 4, 5 };

/* ======================================================================== */
/*  LPC12_REGDEC -- Decode the register set in the filter bank.             */
/* ======================================================================== */
static void IVOICE_HOT(lpc12_regdec)(lpc12_t *f)
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

static uint32_t IVOICE_HOT(sp0256_getb)(int len)
{
    uint32_t data = 0;
    uint32_t d0, d1;

    /* -------------------------------------------------------------------- */
    /*  Fetch data from the FIFO or from the MASK                           */
    /* -------------------------------------------------------------------- */
    if (intellivoice.fifo_sel)
    {
        d0 = intellivoice.fifo[(intellivoice.fifo_tail    ) & 63];
        d1 = intellivoice.fifo[(intellivoice.fifo_tail + 1) & 63];

        data = ((d1 << 10) | d0) >> intellivoice.fifo_bitp;


        /* ---------------------------------------------------------------- */
        /*  Note the PC doesn't advance when we execute from FIFO.          */
        /*  Just the FIFO's bit-pointer advances.   (That's not REALLY      */
        /*  what happens, but that's roughly how it behaves.)               */
        /* ---------------------------------------------------------------- */
        intellivoice.fifo_bitp += len;
        if (intellivoice.fifo_bitp >= 10)
        {
            intellivoice.fifo_tail++;
            intellivoice.fifo_bitp -= 10;
        }
    } else
    {
        /* ---------------------------------------------------------------- */
        /*  Figure out which ROMs are being fetched into, and grab two      */
        /*  adjacent bytes.  The byte we're interested in is extracted      */
        /*  from the appropriate bit-boundary between them.                 */
        /* ---------------------------------------------------------------- */
        int idx0 = (intellivoice.pc    ) >> 3, page0 = idx0 >> 12;
        int idx1 = (intellivoice.pc + 8) >> 3, page1 = idx1 >> 12;

        idx0 &= 0xFFF;
        idx1 &= 0xFFF;

        d0 = d1 = 0;

        if (intellivoice.rom[page0]) d0 = intellivoice.rom[page0][idx0];
        if (intellivoice.rom[page1]) d1 = intellivoice.rom[page1][idx1];

        data = ((d1 << 8) | d0) >> (intellivoice.pc & 7);

        intellivoice.pc += len;
    }

    /* -------------------------------------------------------------------- */
    /*  Mask data to the requested length.                                  */
    /* -------------------------------------------------------------------- */
    data &= ((1u << len) - 1);

    return data;
}


enum sp0256_op_result { SP0256_OP_CONTINUE, SP0256_OP_FRAME, SP0256_OP_XFER };

static INLINE int8_t IVOICE_HOT(sp0256_delta_value)(unsigned len, unsigned shift)
{
    uint32_t raw = sp0256_getb((int)len);
    int32_t value = (int32_t)(raw << (32u - len)) >> (32u - len);
    return (int8_t)(value * (int32_t)(1u << shift));
}

static INLINE void IVOICE_HOT(sp0256_decode_load4)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    memset(&r[B0], 0, 6);
    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
    r[PR] = (uint8_t)sp0256_getb(8);

    switch (mode & 6u)
    {
        case 0u:
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(4) << 3);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B4] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[F4] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            break;

        case 2u:
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(4) << 3);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B4] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[F4] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        case 4u:
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[B4] = (uint8_t)sp0256_getb(8);
            r[F4] = (uint8_t)sp0256_getb(8);
            break;

        case 6u:
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[B4] = (uint8_t)sp0256_getb(8);
            r[F4] = (uint8_t)sp0256_getb(8);
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_loadc)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
    r[PR] = (uint8_t)sp0256_getb(8);

    switch (mode & 6u)
    {
        case 0u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(4) << 3);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B4] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[F4] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            break;

        case 2u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(4) << 3);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B4] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[F4] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        case 4u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[B4] = (uint8_t)sp0256_getb(8);
            r[F4] = (uint8_t)sp0256_getb(8);
            break;

        case 6u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[B4] = (uint8_t)sp0256_getb(8);
            r[F4] = (uint8_t)sp0256_getb(8);
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_load2)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
    r[PR] = (uint8_t)sp0256_getb(8);

    switch (mode & 6u)
    {
        case 0u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(4) << 3);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B4] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[F4] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            break;

        case 2u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(3) << 4);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(5) << 3);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(4) << 3);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B4] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[F4] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        case 4u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[B4] = (uint8_t)sp0256_getb(8);
            r[F4] = (uint8_t)sp0256_getb(8);
            break;

        case 6u:
            r[B0] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F0] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B1] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F1] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B2] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F2] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
            r[B3] = (uint8_t)((uint8_t)sp0256_getb(6) << 1);
            r[F3] = (uint8_t)((uint8_t)sp0256_getb(7) << 1);
            r[B4] = (uint8_t)sp0256_getb(8);
            r[F4] = (uint8_t)sp0256_getb(8);
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        default:
            break;
    }

    r[IA] = (uint8_t)sp0256_getb(5);
    r[IP] = (uint8_t)sp0256_getb(5);

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_setmsba)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);

    switch (mode & 6u)
    {
        case 0u:
        case 2u:
            r[F0] = (uint8_t)((r[F0] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            r[F1] = (uint8_t)((r[F1] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            r[F2] = (uint8_t)((r[F2] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            break;

        case 4u:
        case 6u:
            r[F0] = (uint8_t)((r[F0] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F1] = (uint8_t)((r[F1] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F2] = (uint8_t)((r[F2] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_setmsb6)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);

    switch (mode & 6u)
    {
        case 0u:
            r[F3] = (uint8_t)((r[F3] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F4] = (uint8_t)((r[F4] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            break;

        case 2u:
            r[F3] = (uint8_t)((r[F3] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F4] = (uint8_t)((r[F4] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F5] = (uint8_t)((r[F5] & 0x00u) | ((uint8_t)sp0256_getb(8) << 0));
            break;

        case 4u:
            r[F3] = (uint8_t)((r[F3] & 0x01u) | ((uint8_t)sp0256_getb(7) << 1));
            r[F4] = (uint8_t)((r[F4] & 0x00u) | ((uint8_t)sp0256_getb(8) << 0));
            break;

        case 6u:
            r[F3] = (uint8_t)((r[F3] & 0x01u) | ((uint8_t)sp0256_getb(7) << 1));
            r[F4] = (uint8_t)((r[F4] & 0x00u) | ((uint8_t)sp0256_getb(8) << 0));
            r[F5] = (uint8_t)((r[F5] & 0x00u) | ((uint8_t)sp0256_getb(8) << 0));
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_loade)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
    r[PR] = (uint8_t)sp0256_getb(8);
    (void)mode;

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_loadall)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)sp0256_getb(8);
    r[PR] = (uint8_t)sp0256_getb(8);
    r[B0] = (uint8_t)sp0256_getb(8);
    r[F0] = (uint8_t)sp0256_getb(8);
    r[B1] = (uint8_t)sp0256_getb(8);
    r[F1] = (uint8_t)sp0256_getb(8);
    r[B2] = (uint8_t)sp0256_getb(8);
    r[F2] = (uint8_t)sp0256_getb(8);
    r[B3] = (uint8_t)sp0256_getb(8);
    r[F3] = (uint8_t)sp0256_getb(8);
    r[B4] = (uint8_t)sp0256_getb(8);
    r[F4] = (uint8_t)sp0256_getb(8);

    switch (mode & 6u)
    {
        case 0u:
        case 4u:
            break;

        case 2u:
        case 6u:
            r[B5] = (uint8_t)sp0256_getb(8);
            r[F5] = (uint8_t)sp0256_getb(8);
            break;

        default:
            break;
    }

    r[IA] = (uint8_t)sp0256_getb(8);
    r[IP] = (uint8_t)sp0256_getb(8);

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_delta9)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)(r[AM] + sp0256_delta_value(4u, 2u));
    r[PR] = (uint8_t)(r[PR] + sp0256_delta_value(5u, 0u));

    switch (mode & 6u)
    {
        case 0u:
            r[B0] = (uint8_t)(r[B0] + sp0256_delta_value(3u, 4u));
            r[F0] = (uint8_t)(r[F0] + sp0256_delta_value(3u, 3u));
            r[B1] = (uint8_t)(r[B1] + sp0256_delta_value(3u, 4u));
            r[F1] = (uint8_t)(r[F1] + sp0256_delta_value(3u, 3u));
            r[B2] = (uint8_t)(r[B2] + sp0256_delta_value(3u, 4u));
            r[F2] = (uint8_t)(r[F2] + sp0256_delta_value(3u, 3u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(3u, 3u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(4u, 2u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(4u, 1u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(4u, 2u));
            break;

        case 2u:
            r[B0] = (uint8_t)(r[B0] + sp0256_delta_value(3u, 4u));
            r[F0] = (uint8_t)(r[F0] + sp0256_delta_value(3u, 3u));
            r[B1] = (uint8_t)(r[B1] + sp0256_delta_value(3u, 4u));
            r[F1] = (uint8_t)(r[F1] + sp0256_delta_value(3u, 3u));
            r[B2] = (uint8_t)(r[B2] + sp0256_delta_value(3u, 4u));
            r[F2] = (uint8_t)(r[F2] + sp0256_delta_value(3u, 3u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(3u, 3u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(4u, 2u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(4u, 1u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(4u, 2u));
            r[B5] = (uint8_t)(r[B5] + sp0256_delta_value(5u, 0u));
            r[F5] = (uint8_t)(r[F5] + sp0256_delta_value(5u, 0u));
            break;

        case 4u:
            r[B0] = (uint8_t)(r[B0] + sp0256_delta_value(4u, 1u));
            r[F0] = (uint8_t)(r[F0] + sp0256_delta_value(4u, 2u));
            r[B1] = (uint8_t)(r[B1] + sp0256_delta_value(4u, 1u));
            r[F1] = (uint8_t)(r[F1] + sp0256_delta_value(4u, 2u));
            r[B2] = (uint8_t)(r[B2] + sp0256_delta_value(4u, 1u));
            r[F2] = (uint8_t)(r[F2] + sp0256_delta_value(4u, 2u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(4u, 1u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(5u, 1u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(5u, 0u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(5u, 0u));
            break;

        case 6u:
            r[B0] = (uint8_t)(r[B0] + sp0256_delta_value(4u, 1u));
            r[F0] = (uint8_t)(r[F0] + sp0256_delta_value(4u, 2u));
            r[B1] = (uint8_t)(r[B1] + sp0256_delta_value(4u, 1u));
            r[F1] = (uint8_t)(r[F1] + sp0256_delta_value(4u, 2u));
            r[B2] = (uint8_t)(r[B2] + sp0256_delta_value(4u, 1u));
            r[F2] = (uint8_t)(r[F2] + sp0256_delta_value(4u, 2u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(4u, 1u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(5u, 1u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(5u, 0u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(5u, 0u));
            r[B5] = (uint8_t)(r[B5] + sp0256_delta_value(5u, 0u));
            r[F5] = (uint8_t)(r[F5] + sp0256_delta_value(5u, 0u));
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_setmsb5)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
    r[PR] = (uint8_t)sp0256_getb(8);

    switch (mode & 6u)
    {
        case 0u:
        case 2u:
            r[F0] = (uint8_t)((r[F0] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            r[F1] = (uint8_t)((r[F1] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            r[F2] = (uint8_t)((r[F2] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            break;

        case 4u:
        case 6u:
            r[F0] = (uint8_t)((r[F0] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F1] = (uint8_t)((r[F1] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F2] = (uint8_t)((r[F2] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_deltad)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    switch (mode & 6u)
    {
        case 0u:
            memset(&r[B0], 0, 6);
            r[AM] = (uint8_t)(r[AM] + sp0256_delta_value(4u, 2u));
            r[PR] = (uint8_t)(r[PR] + sp0256_delta_value(5u, 0u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(3u, 3u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(4u, 2u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(4u, 1u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(4u, 2u));
            break;

        case 2u:
            memset(&r[B0], 0, 6);
            r[AM] = (uint8_t)(r[AM] + sp0256_delta_value(4u, 2u));
            r[PR] = (uint8_t)(r[PR] + sp0256_delta_value(5u, 0u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(3u, 3u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(4u, 2u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(4u, 1u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(4u, 2u));
            r[B5] = (uint8_t)(r[B5] + sp0256_delta_value(5u, 0u));
            r[F5] = (uint8_t)(r[F5] + sp0256_delta_value(5u, 0u));
            break;

        case 4u:
            r[AM] = (uint8_t)(r[AM] + sp0256_delta_value(4u, 2u));
            r[PR] = (uint8_t)(r[PR] + sp0256_delta_value(5u, 0u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(4u, 1u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(5u, 1u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(5u, 0u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(5u, 0u));
            break;

        case 6u:
            r[AM] = (uint8_t)(r[AM] + sp0256_delta_value(4u, 2u));
            r[PR] = (uint8_t)(r[PR] + sp0256_delta_value(5u, 0u));
            r[B3] = (uint8_t)(r[B3] + sp0256_delta_value(4u, 1u));
            r[F3] = (uint8_t)(r[F3] + sp0256_delta_value(5u, 1u));
            r[B4] = (uint8_t)(r[B4] + sp0256_delta_value(5u, 0u));
            r[F4] = (uint8_t)(r[F4] + sp0256_delta_value(5u, 0u));
            r[B5] = (uint8_t)(r[B5] + sp0256_delta_value(5u, 0u));
            r[F5] = (uint8_t)(r[F5] + sp0256_delta_value(5u, 0u));
            break;

        default:
            break;
    }

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_setmsb3)(unsigned mode)
{
    uint8_t *r = intellivoice.filt.r;

    r[AM] = (uint8_t)((uint8_t)sp0256_getb(6) << 2);
    r[PR] = (uint8_t)sp0256_getb(8);

    switch (mode & 6u)
    {
        case 0u:
        case 2u:
            r[F0] = (uint8_t)((r[F0] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            r[F1] = (uint8_t)((r[F1] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            r[F2] = (uint8_t)((r[F2] & 0x07u) | ((uint8_t)sp0256_getb(5) << 3));
            break;

        case 4u:
        case 6u:
            r[F0] = (uint8_t)((r[F0] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F1] = (uint8_t)((r[F1] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            r[F2] = (uint8_t)((r[F2] & 0x03u) | ((uint8_t)sp0256_getb(6) << 2));
            break;

        default:
            break;
    }

    r[IA] = (uint8_t)sp0256_getb(5);
    r[IP] = (uint8_t)sp0256_getb(5);

    intellivoice.silent = 0;
}

static INLINE void IVOICE_HOT(sp0256_decode_pause)(unsigned mode)
{
    (void)mode;

    intellivoice.silent = 0;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_rts_setpage)(uint8_t immed4)
{
    if (immed4)
    {
        intellivoice.page = __rev(immed4) >> 13;
        return SP0256_OP_CONTINUE;
    }
    {
        uint32_t target = intellivoice.stack;
        intellivoice.stack = 0;
        if (!target)
        {
            intellivoice.halted = 1;
            intellivoice.pc = 0;
        }
        else
        {
            intellivoice.pc = (int)target;
        }
    }
    return SP0256_OP_XFER;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_setmode)(uint8_t immed4)
{
    intellivoice.mode = ((immed4 & 8u) >> 2) | (immed4 & 4u) | ((immed4 & 3u) << 4);
    return SP0256_OP_CONTINUE;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_jsr)(uint8_t immed4)
{
    int target = (int)(intellivoice.page | (__rev(immed4) >> 17) | (__rev(sp0256_getb(8)) >> 21));
    intellivoice.stack = (intellivoice.pc + 7) & ~7;
    intellivoice.pc = target;
    return SP0256_OP_XFER;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_jmp)(uint8_t immed4)
{
    intellivoice.pc = (int)(intellivoice.page | (__rev(immed4) >> 17) | (__rev(sp0256_getb(8)) >> 21));
    return SP0256_OP_XFER;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_load4)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_load4(intellivoice.mode);
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_loadc)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_loadc(intellivoice.mode);
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_load2)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_load2(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_setmsba)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_setmsba(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_setmsb6)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_setmsb6(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_loade)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_loade(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_loadall)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_loadall(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_delta9)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_delta9(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_setmsb5)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_setmsb5(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_deltad)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_deltad(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_setmsb3)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_setmsb3(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}

static INLINE enum sp0256_op_result IVOICE_HOT(sp0256_op_pause)(uint8_t immed4)
{
    int repeat = (int)(immed4 | (intellivoice.mode & 0x30u));
    if (!repeat) return SP0256_OP_CONTINUE;

    intellivoice.filt.rpt = repeat;
    memset(intellivoice.filt.z_data, 0, sizeof(intellivoice.filt.z_data));
    if ((intellivoice.mode & 2u) == 0u) intellivoice.filt.r[F5] = intellivoice.filt.r[B5] = 0;

    sp0256_decode_pause(intellivoice.mode);
    intellivoice.filt.r[IA] = 0;
    intellivoice.filt.r[IP] = 0;
    intellivoice.silent = 1;
    intellivoice.filt.r[AM] = 0;
    intellivoice.filt.r[PR] = PER_PAUSE;
    lpc12_regdec(&intellivoice.filt);
    return SP0256_OP_FRAME;
}


/* ======================================================================== */
/*  SP0256_MICRO -- Execute complete opcode-specific handlers until a frame */
/*                  is loaded or the sequencer is halted.                   */
/* ======================================================================== */
__attribute__((optimize("O3")))
static void IVOICE_HOT(sp0256_micro)(void)
{
    while (intellivoice.filt.rpt <= 0 && intellivoice.filt.cnt <= 0)
    {
        uint8_t immed4;
        uint8_t opcode;
        enum sp0256_op_result result;

        if (intellivoice.halted && !intellivoice.lrq)
        {
            intellivoice.pc       = intellivoice.ald | (0x1000 << 3);
            intellivoice.fifo_sel = 0;
            intellivoice.halted   = 0;
            intellivoice.lrq      = 0x8000;
            intellivoice.ald      = 0;
        }

        if (intellivoice.halted)
        {
            intellivoice.filt.rpt = 1;
            intellivoice.filt.cnt = 0;
            intellivoice.lrq      = 0x8000;
            intellivoice.ald      = 0;
            return;
        }

        immed4 = (uint8_t)sp0256_getb(4);
        opcode = (uint8_t)sp0256_getb(4);

        switch (opcode)
        {
            case 0x0u: result = sp0256_op_rts_setpage(immed4); break;
            case 0x1u: result = sp0256_op_setmode(immed4); break;
            case 0x2u: result = sp0256_op_load4(immed4); break;
            case 0x3u: result = sp0256_op_loadc(immed4); break;
            case 0x4u: result = sp0256_op_load2(immed4); break;
            case 0x5u: result = sp0256_op_setmsba(immed4); break;
            case 0x6u: result = sp0256_op_setmsb6(immed4); break;
            case 0x7u: result = sp0256_op_loade(immed4); break;
            case 0x8u: result = sp0256_op_loadall(immed4); break;
            case 0x9u: result = sp0256_op_delta9(immed4); break;
            case 0xAu: result = sp0256_op_setmsb5(immed4); break;
            case 0xBu: result = sp0256_op_deltad(immed4); break;
            case 0xCu: result = sp0256_op_setmsb3(immed4); break;
            case 0xDu: result = sp0256_op_jsr(immed4); break;
            case 0xEu: result = sp0256_op_jmp(immed4); break;
            case 0xFu: result = sp0256_op_pause(immed4); break;
            default: result = SP0256_OP_CONTINUE; break;
        }

        if (opcode != 0x1u) intellivoice.mode &= 0xFu;

        if (result == SP0256_OP_XFER)
        {
            intellivoice.fifo_sel = intellivoice.pc == FIFO_ADDR;
            if (intellivoice.fifo_sel && intellivoice.fifo_bitp)
            {
                if (intellivoice.fifo_tail < intellivoice.fifo_head) intellivoice.fifo_tail++;
                intellivoice.fifo_bitp = 0;
            }
            continue;
        }

        if (result == SP0256_OP_FRAME) break;
    }
}

/* ======================================================================== */
/*  IVOICE_MINTY_NEXT_SAMPLE -- Generate one native SP0256 sample directly. */
/* ======================================================================== */
int16_t IVOICE_HOT(ivoice_minty_next_sample)(void)
{
    int16_t sample = 0;

    /*
     * sp0256_micro() contains its own loop and returns only after loading a
     * frame repeat count or reaching the halted state. A second outer retry
     * loop is therefore unnecessary for the direct Minty sample path.
     */
    if (intellivoice.filt.rpt <= 0 && intellivoice.filt.cnt <= 0)
        sp0256_micro();

    if (intellivoice.silent && intellivoice.filt.rpt <= 0 && intellivoice.filt.cnt <= 0)
        return 0;

    if (lpc12_update(&intellivoice.filt, &sample))
        return sample;

    /* Defensive fallback for malformed or unexpected zero-length frames. */
    return 0;
}


/* ======================================================================== */
/*  IVOICE_RD    -- Handle reads from the Intellivoice.                     */
/* ======================================================================== */
uint32_t ivoice_rd(uint32_t addr)
{

    /* -------------------------------------------------------------------- */
    /*  Address 0x80 returns the SP0256 LRQ status on bit 15.               */
    /* -------------------------------------------------------------------- */
    if (addr == 0)
    {
        return intellivoice.lrq;
    }

    /* -------------------------------------------------------------------- */
    /*  Address 0x81 returns the SPB640 FIFO full status on bit 15.         */
    /* -------------------------------------------------------------------- */
    if (addr == 1)
    {
        return (intellivoice.fifo_head - intellivoice.fifo_tail) >= 64 ? 0x8000 : 0;
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
        if (!intellivoice.lrq)
            return;

        /* ---------------------------------------------------------------- */
        /*  Set LRQ to "busy" and load the 8 LSBs of the data into the ALD  */
        /*  reg.  We take the command address, and multiply by 2 bytes to   */
        /*  get the new PC address.                                         */
        /* ---------------------------------------------------------------- */
        intellivoice.lrq = 0;
        intellivoice.ald = (0xFF & data) << 4;

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
            intellivoice.fifo_head = intellivoice.fifo_tail = intellivoice.fifo_bitp = 0;

            memset(&intellivoice.filt, 0, sizeof(intellivoice.filt));
            intellivoice.halted   = 1;
            intellivoice.filt.rpt = -1;
            intellivoice.filt.rng = 1;
            intellivoice.lrq      = 0x8000;
            intellivoice.ald      = 0x0000;
            intellivoice.pc       = 0x0000;
            intellivoice.stack    = 0x0000;
            intellivoice.fifo_sel = 0;
            intellivoice.mode     = 0;
            intellivoice.page     = 0x1000 << 3;
            intellivoice.silent   = 1;
            return;
        }

        /* ---------------------------------------------------------------- */
        /*  If the FIFO is full, drop the data.                             */
        /* ---------------------------------------------------------------- */
        if ((intellivoice.fifo_head - intellivoice.fifo_tail) >= 64)
        {
            return;
        }

        /* ---------------------------------------------------------------- */
        /*  FIFO up the lower 10 bits of the data.                          */
        /* ---------------------------------------------------------------- */
        intellivoice.fifo[intellivoice.fifo_head++ & 63] = data & 0x3FF;

        return;
    }
}


/* ======================================================================== */
/*  IVOICE_INIT  -- Makes a new Intellivoice                                */
/* ======================================================================== */
int ivoice_init(int pal_mode, double time_scale)
{
    (void)pal_mode;
    (void)time_scale;

    memset(&intellivoice, 0, sizeof(intellivoice));
    intellivoice.rom[1] = mask;
    intellivoice.filt.rng = 1;
    intellivoice.halted = 1;
    intellivoice.filt.rpt = -1;
    intellivoice.lrq = 0x8000;
    intellivoice.page = 0x1000 << 3;
    intellivoice.silent = 1;

    /* -------------------------------------------------------------------- */
    /*  Do a software-style reset of the Intellivoice.                      */
    /* -------------------------------------------------------------------- */
    ivoice_wr(1, 0x400);

    return 0;
}


