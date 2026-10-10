// Host-buildable test driver for fujibus.c -- no pico-sdk involved.
#include <stdio.h>
#include "fujibus.h"

int main(void)
{
    if (!fujibus_selftest()) {
        fprintf(stderr, "fujibus_selftest: FAIL\n");
        return 1;
    }
    printf("fujibus_selftest: PASS\n");
    return 0;
}
