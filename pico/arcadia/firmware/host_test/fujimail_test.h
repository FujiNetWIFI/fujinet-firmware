/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "arcmap.h"

#define FUJIMAIL_TEST_GATE   arcmap_gate
#define FUJIMAIL_TEST_GAME   ARCMAP_STORE           /* the largest APPBANK image */
#define FUJIMAIL_TEST_TOOBIG (ARCMAP_STORE + 0x800u)
