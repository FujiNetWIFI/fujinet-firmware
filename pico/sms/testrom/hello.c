/* hello.c -- milestone 0: the toolchain, the VDP and the font. */

#include "fujidisp.h"
#include "fujilib.h"

void main(void)
{
    disp_init();
    disp_at(5, 2, "FUJINET MASTER SYSTEM");
    disp_at(5, 4, fn_present() ? "FUJINET CART PRESENT" : "NO FUJINET CART");
    disp_highlight(1);
    disp_at(5, 6, " HELLO, WORLD ");
    disp_highlight(0);
    for (;;)
        ;
}
