/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "smsmap.h"

#define FUJIMAIL_TEST_GATE   smsmap_gate
#define FUJIMAIL_TEST_GAME   (128u * 1024)
#define FUJIMAIL_TEST_TOOBIG (SMSMAP_IMAGE_MAX + 0x4000u)
