/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "s2map.h"

#define FUJIMAIL_TEST_GATE   s2map_gate
#define FUJIMAIL_TEST_GAME   (S2MAP_IMAGE_MAX & ~511u)
#define FUJIMAIL_TEST_TOOBIG (S2MAP_IMAGE_MAX + 0x4000u)
