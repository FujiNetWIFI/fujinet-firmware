/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "a78map.h"

#define FUJIMAIL_TEST_GATE   a78map_gate
#define FUJIMAIL_TEST_GAME   (128u * 1024)
#define FUJIMAIL_TEST_TOOBIG (A78MAP_IMAGE_MAX + 0x4000u)
