/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "nesmap.h"

#define FUJIMAIL_TEST_GATE   nesmap_gate
#define FUJIMAIL_TEST_GAME   (128u * 1024)
#define FUJIMAIL_TEST_TOOBIG (NESMAP_IMAGE_MAX + 1u)
