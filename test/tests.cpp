#include <unity.h>
#include <string.h>
#include "tests.h"
#include "tests_networkprotocol_base.h"

/**
 * Instantiated protocol object
 */
NetworkProtocol *protocol;

/**
 * Test main
 */
void app_main()
{
    ets_delay_us(5000000); // Odd, I actually had to add this so I wouldn't miss the first bit of data.

    UNITY_BEGIN();
    tests_networkprotocol_base();
    UNITY_END();
}
