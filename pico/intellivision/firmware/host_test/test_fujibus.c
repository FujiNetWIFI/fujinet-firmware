// Host test for fujibus.c.
// Run: gcc -Wall -Wextra -I../include -o /tmp/test_fujibus test_fujibus.c ../src/fujibus.c && /tmp/test_fujibus
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
