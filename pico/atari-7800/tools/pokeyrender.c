/* pokeyrender.c -- the firmware's POKEY synthesiser as a host program.
 *
 * Reads "clock register value" lines (machine clocks at 1.79 MHz) on stdin
 * and writes raw signed 16-bit mono samples, one every 57 clocks (31.4 kHz),
 * to stdout until the last clock given. tools/pokeycmp.py drives it.
 */

#include <stdio.h>
#include <stdlib.h>

#include "pokey.h"

int main(void)
{
    pokey_t p;
    unsigned long long clk, end = 0;
    unsigned reg, val;
    uint8_t lv;

    pokey_init(&p);
    while (scanf("%llu %x %x", &clk, &reg, &val) == 3) {
        while (p.t + 57 <= clk) {
            int16_t s;

            pokey_render(&p, &lv, 1, 57);
            s = (int16_t)(lv * 512);
            fwrite(&s, 2, 1, stdout);
        }
        pokey_write(&p, reg, (uint8_t)val, p.t);
        end = clk;
    }
    (void)end;
    return 0;
}
