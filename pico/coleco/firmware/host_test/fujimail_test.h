/* fujimail_test.h -- this cart's mapper, for pico/common/host_test/test_fujimail.c. */
#include "colmap.h"

#define FUJIMAIL_TEST_GATE   colmap_gate
#define FUJIMAIL_TEST_GAME   (128u * 1024)          /* the stock Pico's FUJI_STORE_RAM_SIZE */
#define FUJIMAIL_TEST_TOOBIG (COLMAP_XIN1_2M + 0x4000u)
