/**
 * #FujiNet Unit Tests - Main
 */

#include <unity.h>
#include <esp32/rom/ets_sys.h>
#include "test_pass.h"
#include "test_networkprotocol_translation.h"

extern "C"
{
    /**
     * Main Entry point
     */
    void app_main();
}

void app_main()
{
    ets_delay_us(5000000);

    UNITY_BEGIN();

    test_pass_run();
    tests_networkprotocol_translation();    

    UNITY_END();
}