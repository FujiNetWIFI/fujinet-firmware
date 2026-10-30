/* fujidisp.h -- the display layer every client shares: OS7's Graphics 1 text screen,
 * 32x24, through mode_1, load_ascii and put_vram.
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
/* Write `str` at (col,row), clipped to the row. Not NUL-padded: whatever was
 * there stays, so callers that need a clean row call disp_row_clear first. */
void disp_at(unsigned char col, unsigned char row, const char *str);
void disp_row_clear(unsigned char row);
/* One character. Used to paint names straight out of the cartridge's reply
 * window, which is volatile and therefore not a `const char *`. */
void disp_char(unsigned char col, unsigned char row, char chr);
/* Write a string, optionally drawn from the inverse charset. Cheaper than
 * disp_row_invert for anything whose text we already have in hand -- no VRAM
 * read-back -- and it works on part of a row, which the row version cannot. */
void disp_at_hi(unsigned char col, unsigned char row, const char *str, bool highlight);

/* One character, optionally inverse. */
void disp_char_hi(unsigned char col, unsigned char row, char chr, bool highlight);

/* The selection bar: a row's characters re-pointed at the inverse charset, or back. It
 * reads the row from the name table, since the names came from the reply window and no copy
 * is kept. Idempotent. */
void disp_row_invert(unsigned char row, bool on);

/* Right-aligned unsigned decimal, for lengths and error codes. */
void disp_at_u16(unsigned char col, unsigned char row, unsigned int value);
void disp_at_hex8(unsigned char col, unsigned char row, unsigned char value);

#endif /* FUJIDISP_H */
