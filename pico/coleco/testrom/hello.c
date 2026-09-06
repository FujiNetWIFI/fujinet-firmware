/* hello.c -- milestone 0: prove the toolchain, the cartridge header and the
 * display layer, with no mailbox involved at all.
 *
 * If this shows up in MAME the z88dk +coleco link line is right, os7lib is
 * linked, the image is a valid 32K cartridge, and build.sh's padding and claim
 * stamp did not break the header. That is the whole job.
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
