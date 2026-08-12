/* emu2149.h */
#ifndef _EMU2149_H_
#define _EMU2149_H_

#include <stdint.h>

#define EMU2149_VOL_DEFAULT 1
#define EMU2149_VOL_YM2149 1
#define EMU2149_VOL_AY_3_8910 2

#define PSG_MASK_CH(x) (1<<(x))

typedef struct __PSG {

  /* Volume Table */
  uint32_t *voltbl;

  uint8_t reg[0x20];
  int32_t out;

  uint32_t clk, rate, base_incr;
  uint8_t quality;
  uint8_t clk_div;

  uint16_t count[3];
  uint8_t volume[3];
  uint16_t freq[3];
  uint8_t edge[3];
  uint8_t tmask[3];
  uint8_t nmask[3];
  uint32_t mask;

  uint32_t base_count;

  uint8_t env_ptr;
  uint8_t env_face;

  uint8_t env_continue;
  uint8_t env_attack;
  uint8_t env_alternate;
  uint8_t env_hold;
  uint8_t env_pause;

  uint16_t env_freq;
  uint32_t env_count;

  uint32_t noise_seed;
  uint8_t noise_scaler;
  uint8_t noise_count;
  uint8_t noise_freq;

  /* rate converter */
  uint32_t realstep;
  uint32_t psgtime;
  uint32_t psgstep;

  uint32_t freq_limit;

  /* I/O Ctrl */
  uint8_t adr;

  /* output of channels */
  int16_t ch_out[3];

} PSG;

void PSG_set_quality (PSG * psg, uint32_t q);
void PSG_set_rate (PSG * psg, uint32_t r);
PSG *PSG_new (uint32_t clk, uint32_t rate);
void PSG_reset (PSG *);
void PSG_delete (PSG *);
void PSG_writeReg (PSG *, uint8_t reg, uint8_t val);
void PSG_writeIO (PSG * psg, uint32_t adr, uint32_t val);
uint8_t PSG_readReg (PSG * psg, uint32_t reg);
uint8_t PSG_readIO (PSG * psg);
int16_t PSG_calc (PSG *);
void PSG_setVolumeMode (PSG * psg, int type);
void PSG_setClockDivider(PSG *psg, uint8_t enable);
uint32_t PSG_setMask (PSG *, uint32_t mask);
uint32_t PSG_toggleMask (PSG *, uint32_t mask);
    
#endif
