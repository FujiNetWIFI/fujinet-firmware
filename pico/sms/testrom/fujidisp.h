/* fujidisp.h -- 32x24 text on the SMS VDP in Mode 4.
 *
 * The font is z88dk's 8x8 standard_font, tiles 32-127 = ASCII. Highlighted
 * text uses the sprite palette, which is the background palette inverted.
 */

#ifndef FUJIDISP_H
#define FUJIDISP_H

#define DISP_COLS 32
#define DISP_ROWS 24

void disp_init(void);
void disp_cls(void);
void disp_row_clear(unsigned char row);
void disp_highlight(unsigned char on);
void disp_at(unsigned char x, unsigned char y, const char *s);
void disp_at_n(unsigned char x, unsigned char y, const char *s, unsigned char n);
void disp_at_u16(unsigned char x, unsigned char y, unsigned int v);
void disp_at_hex8(unsigned char x, unsigned char y, unsigned char v);

#endif /* FUJIDISP_H */
