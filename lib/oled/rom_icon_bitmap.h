#ifndef ROM_ICON_BITMAP_H
#define ROM_ICON_BITMAP_H

#include <cstdint>

#define ROM_ICON_W 8
#define ROM_ICON_H 8

// Hand-drawn 8x8 1bpp glyph (a DIP-chip outline), one SSD1306 page tall, one
// byte per column. Sized to match the status view's 8px list row height.
static const uint8_t rom_icon_bitmap[ROM_ICON_W] = {
    0x7E, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0x7E,
};

#endif // ROM_ICON_BITMAP_H
