/* fujisnd.h -- the SN76489, kept quiet. It powers up at full volume, and a $55AA header
 * skips the title screen that would silence it, so snd_init() comes first in every
 * client. The click is two port writes; OS7's sound routines would cost RAM for a blip.
 */

#ifndef FUJISND_H
#define FUJISND_H

/* Silence all four channels. Call before anything else. */
void snd_init(void);

/* A short click, for cursor movement. Decays through snd_tick(). */
void snd_click(void);

/* Call once per vblank, from the NMI handler. */
void snd_tick(void);

#endif /* FUJISND_H */
