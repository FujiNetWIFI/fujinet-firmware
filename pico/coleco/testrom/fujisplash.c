#include <os7.h>

#include "fujidisp.h"
#include "fujisplash.h"

/* Colour groups 16-27 cover patterns 0x80-0xDF, which is the whole logo. The
 * artwork allocates its glyphs roughly top-to-bottom, so this reads on screen
 * as a sweep down the wordmark: white at the mast, warming through yellow to
 * deep red at the base. Low nibble 0 is transparent, which shows the black
 * backdrop -- exactly how the OS7 banner is coloured. */
static const unsigned char logo_colors[12] = {
    0xF0, 0xF0,                 /* white       */
    0xB0, 0xB0,                 /* light yellow */
    0xA0, 0xA0,                 /* dark yellow  */
    0x90, 0x90,                 /* light red    */
    0x80, 0x80,                 /* medium red   */
    0x60, 0x60                  /* dark red     */
};

static void centre(unsigned char row, const char *s)
{
    unsigned char n = 0;

    while (s[n] != '\0')
        n++;
    disp_at((unsigned char)(n >= DISP_COLS ? 0 : (DISP_COLS - n) / 2), row, s);
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

    /* One row at a time, because OS7's PUT_VRAM truncates its count to 8 bits.
     * Asking for all 320 bytes at once writes 320 & 0xFF = 64 -- two rows --
     * and returns as though it had done the job. Nothing warns; the logo just
     * comes out with its top two rows and nothing else. The 96-pattern write
     * above is safe because 96 is the number of ENTRIES, not bytes, and OS7
     * scales it internally to 768. Keep every count here under 256. */
    {
        unsigned char r;

        for (r = 0; r < 10; r++)
            put_vram(PATTERN_NAME_TABLE,
                     (unsigned short)(SPLASH_ROW + r) * DISP_COLS,
                     (void *)(splash_nametable + (unsigned int)r * DISP_COLS),
                     DISP_COLS);
    }

    /* Exactly 32 characters, so it fills the row edge to edge. */
    disp_at(0, 18, "NETWORK ADAPTER FOR COLECOVISION");
    centre(21, "PRESS FIRE");
}
