/* fujisplash.h -- the boot splash. A $55AA header skips the OS7 title screen, so this takes
 * its place in the same shape: the FujiNet wordmark on black, a colour sweep across its
 * twelve pattern groups, centred white text below. The artwork is
 * github.com/tschak909/fujinet-adam-logo (GPLv3); fujisplash_data.c is generated from its
 * src/logo.c: logo_patterns whole and the first 320 bytes of logo_nametable.
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
