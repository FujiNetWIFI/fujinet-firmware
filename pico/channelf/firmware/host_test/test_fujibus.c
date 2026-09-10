// Host-buildable test driver for fujibus.c -- no RP2040/pico-sdk involved.
// Run: gcc -Wall -Wextra -I.. -o /tmp/test_fujibus test_fujibus.c ../fujibus.c && /tmp/test_fujibus
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
