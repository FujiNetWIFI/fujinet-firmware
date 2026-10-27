// Runs the fujibus.c self-test on the host.
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
