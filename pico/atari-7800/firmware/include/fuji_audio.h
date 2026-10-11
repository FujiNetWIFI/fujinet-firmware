/* fuji_audio.h -- the cart's POKEY sound, out of a PWM into the edge's audio
 * input (pin 18), and the POKEY read registers core1 serves. */

#ifndef FUJI_AUDIO_H
#define FUJI_AUDIO_H

#include <stdint.h>

void fuji_audio_init(void);
void fuji_audio_write(unsigned reg, uint8_t v, uint32_t cycle);
void fuji_audio_reset(void);               /* a new image: silence */
void fuji_audio_service(void);             /* keep the DMA fed */

#endif /* FUJI_AUDIO_H */
