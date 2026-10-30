#include <os7.h>

#include "fujidisp.h"
#include "fujisplash.h"

/* Colour groups 16-27, the logo's patterns, roughly top to bottom: a sweep from white to
 * deep red over the transparent (black) backdrop, as the OS7 banner is coloured. */
static const unsigned char logo_colors[12] = {
    0xF0, 0xF0,                 /* white       */
    0xB0, 0xB0,                 /* light yellow */
    0xA0, 0xA0,                 /* dark yellow  */
    0x90, 0x90,                 /* light red    */
    0x80, 0x80,                 /* medium red   */
    0x60, 0x60                  /* dark red     */
};

static void centre(unsigned char row, const char *text)
{
    unsigned char len = 0;

    while (text[len] != '\0')
        len++;
    disp_at((unsigned char)(len >= DISP_COLS ? 0 : (DISP_COLS - len) / 2), row, text);
}

void splash_show(void)
{
    mode_1();
    load_ascii();

    /* Text groups white on transparent, like the OS7 splash's own; the logo's
     * groups take the gradient. */
    fill_vram(0x2000, 32, 0xF0);
    put_vram(PATTERN_COLOR_TABLE, 16, (void *)logo_colors, 12);
    write_register(REGISTER_BACKGROUND, BLACK);

    disp_cls();
    put_vram(PATTERN_GENERATOR_TABLE, SPLASH_PATTERN,
             (void *)splash_patterns, 96);

    /* A row at a time: PUT_VRAM truncates its count to 8 bits, so 320 bytes would write 64.
     * The 96 above is a count of patterns, which OS7 scales to bytes itself. */
    {
        unsigned char line;

        for (line = 0; line < 10; line++)
            put_vram(PATTERN_NAME_TABLE,
                     (unsigned short)(SPLASH_ROW + line) * DISP_COLS,
                     (void *)(splash_nametable + (unsigned int)line * DISP_COLS),
                     DISP_COLS);
    }

    /* Exactly 32 characters, so it fills the row edge to edge. */
    disp_at(0, 18, "NETWORK ADAPTER FOR COLECOVISION");
    centre(21, "PRESS FIRE");
}
