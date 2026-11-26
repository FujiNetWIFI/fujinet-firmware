/* fujimail_test.h -- this cart's push gate, for pico/common/host_test/test_fujimail.c. */
#include "vcs_cart.h"

#define FUJIMAIL_TEST_GATE   vcs_gate
#define FUJIMAIL_TEST_GAME   VCS_IMAGE_MAX
#define FUJIMAIL_TEST_TOOBIG (VCS_IMAGE_MAX + FN_BANK_SIZE)
#define FUJIMAIL_TEST_SMALL  FN_BANK_SIZE
