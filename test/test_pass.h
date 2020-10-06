/**
 * #FujiNet tests - A simple sanity check
 */

#include <unity.h>

#ifndef TEST_PASS_H
#define TEST_PASS_H

#ifdef __cplusplus

extern "C"
{
    /**
     * Simple sanity test, always passes.
     */
    void test_pass();

    /**
     * Run point
     */
    void test_pass_run();
}

#endif

#endif /* TEST_PASS_H */