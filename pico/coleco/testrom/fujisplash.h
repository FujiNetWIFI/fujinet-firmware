/* fujisplash.h -- the boot splash.
 *
 * Every ColecoVision cartridge shows the OS7 title screen before it runs;
 * ours skips it (the $55AA header) and so has to put something there itself.
 * This is that screen, built to the same shape as the real one -- measured
 * rather than remembered: OS7 draws a multicoloured banner near the top on a
 * BLACK backdrop, with centred white text lower down, its colour table running
 * 0xF0 (white on transparent) for the text groups and a magenta/red/yellow/
 * green/blue sweep across the banner's own pattern groups.
 *
 * Here the FujiNet wordmark takes the banner's place, in the middle of the
 * screen, with the same trick: its 96 glyphs span twelve colour groups and the
 * artwork happens to allocate them roughly top-to-bottom, so assigning a
 * gradient across those groups paints a sweep down the logo.
 *
 * The artwork is from ~/Workspace/fujinet-adam-logo (Thomas Cherryhomes, 2021,
 * GPLv3); see fujisplash_data.c. Regenerate that file with:
 *
 *     python3 - reads logo.c, strips // comments, takes logo_patterns whole
 *     and the first 320 bytes of logo_nametable (10 graphic rows, dropping the
 *     ADAM strapline), and emits both as C arrays.
 */

#ifndef FUJISPLASH_H
#define FUJISPLASH_H

#define SPLASH_ROW      6       /* top row of the 10-row logo */
#define SPLASH_PATTERN  0x80    /* baked into the name-table data */

extern const unsigned char splash_patterns[768];
extern const unsigned char splash_nametable[320];

/* Take over the screen and draw the splash. Leaves the VDP in Graphics 1 with
 * the logo glyphs loaded at 0x80 -- so the caller must run disp_init() again
 * before drawing anything else, which rebuilds the ASCII and inverse sets. */
void splash_show(void);

#endif /* FUJISPLASH_H */
