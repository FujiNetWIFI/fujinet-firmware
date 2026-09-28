#ifndef CYD_LCD_H
#define CYD_LCD_H

/* Minimal ILI9341 driver for CYD boards: landscape 320x240, RGB565,
 * filled rectangles and MC6847-font text. Not thread-safe; call it from a
 * single task (the CYD UI task). */

#include <stdint.h>

#define CYD_LCD_WIDTH  320
#define CYD_LCD_HEIGHT 240

// RGB565 from 8-bit components
#define CYD_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

bool cyd_lcd_init();
void cyd_lcd_fill(int x, int y, int w, int h, uint16_t color);

// Draw text in the MC6847 font, each font pixel scaled to sx by sy.
// Lower case prints as upper case (as on a CoCo); unknown chars as '?'.
void cyd_lcd_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int sx, int sy);

#endif // CYD_LCD_H
