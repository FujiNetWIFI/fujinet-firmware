#include <stdlib.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/structs/pwm.h"

#include "board.h"
#include "audio.h"
#include "emu2149.h"
#include "ivoice.h"
#include "intellicart.h"

extern Cartridge cart;     // main data structure for cart emulation

static repeating_timer_t timer;
PSG* psg0;
uint8_t AudioVolume;

const uint8_t ECS_LUT[16] = {
   0x00,
   0x02,
   0x04,
   0x0B,
   0x01,
   0x03,
   0x05,
   0x0C,
   0x07,
   0x06,
   0x0D,
   0x08,
   0x09,
   0x0A,
   0x0E,
   0x0F
};

#define GPIO_TO_SLICE(g) ((g) < 32 ? (((g) >> 1) & 7) : (8 + (((g) >> 1) & 3)))

bool  __not_in_flash_func(audio_callback)(repeating_timer_t *rt) {   
   static int32_t ecs_raw = 0;
   static int32_t ivoice_raw = 0;
   static uint8_t audio_cycle = 0;



   /* first action is to output sample from previous cycle, this way processing speed doesn't affect sample timing */
#ifndef NDEBUG
   // debug output for audio callback every 0.2 seconds (8000 callbacks at 40kHz)
   static uint16_t cnt = 0;
   if (cnt++ >= 8000) {
      cnt = 0;
      printf("audio_callback: ecs=%08lX, ivoice=%08lX\n", ecs_raw,ivoice_raw); 
   }
#else
   /* apply 8-bit volume control, normalize to 10-bit PWM */
   uint16_t pwm = (uint16_t)((((uint32_t)(ecs_raw+ivoice_raw) * (uint32_t)(AudioVolume + 1) / 3) >> 10));
   // clamp to 10-bit range
   if (pwm > 1023) pwm = 1023;
   pwm_hw->slice[GPIO_TO_SLICE(AUDIO_PIN)].cc = pwm;
#endif

   if (cart.ECSSupport) {
      PSG_calc(psg0);
      // ecs audio output is ranging from 0 to 4095 per channel, the mixed output ranges from 0 to 12285
      ecs_raw = (int32_t)psg0->out;
   }

   if (cart.IntellivoiceSupport && audio_cycle == 3) {
      //gpio_put(LED, false);
      // Intellivoice output is generated at 1/4 the ECS sample rate, so only generate every 4th callback
      // Intellivoice output is signed, ranging from -32768 to 32767.
      ivoice_raw = (int32_t)ivoice_minty_next_sample();
      // scale to match ECS output level
      ivoice_raw = ((ivoice_raw + 32768) >> 2);
      //gpio_put(LED, true);
   }

   audio_cycle = (audio_cycle + 1) & 0x03;

   return true;
}


void init_audio(uint8_t tv_mode, uint8_t volume) {

   AudioVolume = volume;

#ifndef NDEBUG
   printf("init_audio: tv_mode=%d, volume=%d\n", tv_mode, AudioVolume);
#else
   gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
   uint audioSlice = pwm_gpio_to_slice_num(AUDIO_PIN);
 
   pwm_config cfg = pwm_get_default_config();
   pwm_config_set_clkdiv(&cfg, 1.0f);
   pwm_config_set_wrap(&cfg, PWM_WRAP);
   pwm_init(audioSlice, &cfg, true);
#endif

   if (cart.ECSSupport) {
      if (tv_mode == 0) 
         psg0 = PSG_new(PAL_ECS_FREQ/2, AUDIO_FREQ);
      else
         psg0 = PSG_new(NTSC_ECS_FREQ/2, AUDIO_FREQ);
   

      PSG_reset(psg0);
      PSG_setVolumeMode(psg0, EMU2149_VOL_AY_3_8910);
   }

   if (cart.IntellivoiceSupport) {
      const int pal_mode = (tv_mode == 0u) ? 1 : 0;

      ivoice_init(pal_mode, 1.0);
   }

   add_repeating_timer_us(-(AUDIO_PERIOD), audio_callback, NULL, &timer);

}
