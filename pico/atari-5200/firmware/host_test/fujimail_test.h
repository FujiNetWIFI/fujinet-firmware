/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "a52map.h"

#define FUJIMAIL_TEST_GATE   a52map_gate
#define FUJIMAIL_TEST_GAME   (128u * 1024)
#define FUJIMAIL_TEST_TOOBIG (A52MAP_IMAGE_MAX + 0x4000u)
