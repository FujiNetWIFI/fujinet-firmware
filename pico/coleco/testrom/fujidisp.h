/* fujidisp.h -- the small display and input layer every client shares.
 *
 * Everything here goes through OS7, the ColecoVision's own BIOS, rather than
 * touching the VDP directly: mode_1 lays out the standard Graphics-1 tables,
 * load_ascii fills the pattern generator with the BIOS's own ASCII set, and
 * put_vram writes into the pattern name table. That gives a 32x24 text screen
 * for free -- roomier than any other console in this family -- and it is the
 * point of building the client against os7lib in the first place.
 */

#ifndef FUJIDISP_H
#define FUJIDISP_H

#include <os7.h>
#include <stdbool.h>

#define DISP_COLS 32
#define DISP_ROWS 24

/* Graphics 1, the BIOS ASCII charset, white on dark blue. */
void disp_init(void);
void disp_cls(void);
/* Write `s` at (col,row), clipped to the row. Not NUL-padded: whatever was
 * there stays, so callers that need a clean row call disp_row_clear first. */
void disp_at(unsigned char col, unsigned char row, const char *s);
void disp_row_clear(unsigned char row);
/* One character. Used to paint names straight out of the cartridge's reply
 * window, which is volatile and therefore not a `const char *`. */
void disp_char(unsigned char col, unsigned char row, char c);
/* Right-aligned unsigned decimal, for lengths and error codes. */
void disp_at_u16(unsigned char col, unsigned char row, unsigned int v);
void disp_at_hex8(unsigned char col, unsigned char row, unsigned char v);

#endif /* FUJIDISP_H */
