/* hello.c -- milestone 0: the toolchain, the cartridge header and the display layer, with
 * no mailbox at all.
 */

#include <os7.h>

#include "fujidisp.h"
#include "fujisnd.h"

void main(void)
{
    snd_init();     /* the PSG powers up buzzing; see fujisnd.h */
    disp_init();
    disp_at(6, 8,  "FUJINET COLECOVISION");
    disp_at(6, 10, "HELLO FROM OS7");
    disp_at(6, 12, "MILESTONE 0");

    for (;;)
        ;
}
