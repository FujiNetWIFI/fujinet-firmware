/* fujisnd.h -- the SN76489, kept quiet.
 *
 * A ColecoVision powers its sound chip up with all four channels at
 * attenuation 0 -- full volume -- and their frequency registers at zero. The
 * result is an immediate, permanent buzz. Every commercial cartridge is
 * spared this because the BIOS title screen silences the chip before the game
 * runs; a client whose header magic is $55AA skips the title screen and gets
 * the noise instead. So snd_init() is not optional politeness, it is the first
 * thing any client here must do.
 *
 * The click is done with direct port writes rather than OS7's SOUND_INIT /
 * PLAY_IT / SOUND_MAN. That trio wants a note list and a per-sound RAM data
 * area, which on a machine with ~700 usable bytes is a lot to spend on a
 * cursor blip -- and the blip is two register writes. Everything else in these
 * clients goes through OS7; this is the one place where it costs more than it
 * saves.
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
