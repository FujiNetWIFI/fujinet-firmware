#ifndef AUDIO_H
#define AUDIO_H

#define PWM_WRAP 1024

#define PAL_ECS_FREQ       4000000
#define NTSC_ECS_FREQ      3579545
#define AUDIO_PERIOD       25
#define AUDIO_FREQ         (1000000/AUDIO_PERIOD)
#define INTELLIVOICE_FREQ  10000

void init_audio(uint8_t tv_mode, uint8_t volume);

#endif
