/**
 * #FujiNet Tests - All pass Test (Sanity check)
 */

#include "test_pass.h"

void test_pass_run()
{
    RUN_TEST(test_pass);
}

void test_pass()
{
    TEST_ASSERT_EQUAL_INT(1,1);
}