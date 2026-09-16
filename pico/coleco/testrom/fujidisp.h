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

/* The inverse charset lives at pattern 0x80, a verbatim copy of the ASCII set
 * at 0x20 with the colour nibbles swapped. See disp_init(). */
#define DISP_INVERSE 0x60

/* Graphics 1, the BIOS ASCII charset, white on dark blue, screen and vblank
 * interrupt both on. Safe to call more than once and in any order relative to
 * in_init(). */
void disp_init(void);
void disp_cls(void);
/* Write `s` at (col,row), clipped to the row. Not NUL-padded: whatever was
 * there stays, so callers that need a clean row call disp_row_clear first. */
void disp_at(unsigned char col, unsigned char row, const char *s);
void disp_row_clear(unsigned char row);
/* One character. Used to paint names straight out of the cartridge's reply
 * window, which is volatile and therefore not a `const char *`. */
void disp_char(unsigned char col, unsigned char row, char c);
/* Write a string, optionally drawn from the inverse charset. Cheaper than
 * disp_row_invert for anything whose text we already have in hand -- no VRAM
 * read-back -- and it works on part of a row, which the row version cannot. */
void disp_at_hi(unsigned char col, unsigned char row, const char *s, bool hi);

/* One character, optionally inverse. */
void disp_char_hi(unsigned char col, unsigned char row, char c, bool hi);

/* Turn a whole row inverse (dark blue on white) or back again. This is the
 * selection bar: the ADAM CONFIG gets the same effect from Graphics 2's
 * per-cell colour table, which we do not have in Graphics 1, so instead the
 * row's characters are re-pointed at an inverted copy of the charset.
 *
 * The row is read back out of the name table rather than redrawn, because
 * there is nothing to redraw it FROM -- the file browser paints names straight
 * out of the mailbox reply window and never keeps a copy. So moving the bar
 * costs two 32-byte VRAM round trips and no network traffic. Idempotent in
 * both directions. */
void disp_row_invert(unsigned char row, bool on);

/* Right-aligned unsigned decimal, for lengths and error codes. */
void disp_at_u16(unsigned char col, unsigned char row, unsigned int v);
void disp_at_hex8(unsigned char col, unsigned char row, unsigned char v);

#endif /* FUJIDISP_H */
