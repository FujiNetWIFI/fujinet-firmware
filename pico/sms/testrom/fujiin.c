/* fujiin.c -- the joypad in port 1 and the Reset button. */

#include <arch/sms.h>

#include "fujiin.h"

unsigned char in_pad(void)
{
    unsigned char v = (unsigned char)(~IO_DC & 0x3F);

    if (!(IO_DD & 0x10))
        v |= IN_RESET;
    return v;
}

unsigned char in_wait(void)
{
    unsigned char b;
    unsigned int settle;

    while (in_pad())
        ;
    while (!(b = in_pad()))
        ;
    for (settle = 0; settle < 2000; settle++)
        ;
    while (in_pad())
        ;
    return b;
}
