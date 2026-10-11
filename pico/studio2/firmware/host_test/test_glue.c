/* test_glue.c -- the Rev0 glue, as truth tables: the 74HCT541 drives D0-D7
 * only while /MRD and /DRIVE are low, and CART CS (which turns console RAM
 * off) is NOR(/MRD, /CLAIM) -- each only while the console is powered. The
 * KiCad check_glue reads the same equations from s2_cart.h.
 */

#include <assert.h>
#include <stdio.h>

#include "s2_cart.h"

int main(void)
{
    unsigned m;

    /* /MRD high: no read in progress, nothing from the cart whatever the RP
     * says -- the bus is let go by the console's own strobe. */
    assert(!s2_glue_drive(true, false, true));
    assert(!s2_glue_drive(true, true, true));
    assert(!s2_glue_cartcs(true, false, true));
    assert(!s2_glue_cartcs(true, true, true));

    /* /MRD low, console powered: the RP decides. */
    assert(s2_glue_drive(false, false, true));
    assert(!s2_glue_drive(false, true, true));
    assert(s2_glue_cartcs(false, false, true));
    assert(!s2_glue_cartcs(false, true, true));

    /* Console off: /MRD sinks with its rail, but nothing is driven into it,
     * whatever the RP (powered from USB) last left on its pins. */
    for (m = 0; m < 4; m++) {
        assert(!s2_glue_drive(m & 1, m & 2, false));
        assert(!s2_glue_cartcs(m & 1, m & 2, false));
    }

    printf("test_glue: all passed\n");
    return 0;
}
