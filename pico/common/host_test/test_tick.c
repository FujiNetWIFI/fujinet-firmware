/* test_tick.c -- the value the cart paints at FN_R_TICK_LO/HI.
 *
 * Expectations from the prose in fuji_mailbox.h: 60 counts a second from cart
 * power-up, monotonic, wrapping at 65536. Checked against floor(us * 60 / 1e6)
 * computed here in long double, never by calling the code under test twice.
 */
#include <stdint.h>
#include <stdio.h>

#include "fuji_tick.h"

static int fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "test_tick: FAIL %s:%d: ", __FILE__, __LINE__);  \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, "\n");                                           \
            fails++;                                                         \
        }                                                                    \
    } while (0)

static uint16_t want(uint64_t micros)
{
    return (uint16_t)(uint64_t)((long double)micros * 60.0L / 1000000.0L);
}

int main(void)
{
    CHECK(fuji_tick_value(0) == 0, "starts at 0");
    CHECK(fuji_tick_value(16666) == 0, "16.666 ms is still tick 0");
    CHECK(fuji_tick_value(16667) == 1, "16.667 ms is tick 1");
    CHECK(fuji_tick_value(1000000) == 60, "one second is 60");
    CHECK(fuji_tick_value(10000000) == 600, "ten seconds is 600");

    /* wraps at 65536 ticks = 1092.2666 s */
    uint64_t wrap = 65536ull * 1000000ull / 60ull; /* 1092266666 us */
    CHECK(fuji_tick_value(wrap - 1) == 65535, "last value before the wrap");
    CHECK(fuji_tick_value(wrap + 17) == 0, "wraps to 0");

    /* monotonic in steps of 0 or 1 (mod 65536), across a day at 4167 us,
     * the repaint period -- and against the long-double expectation */
    uint16_t prev = 0;
    for (uint64_t micros = 0; micros < 86400ull * 1000000ull; micros += 4167) {
        uint16_t tick = fuji_tick_value(micros);
        uint16_t step = (uint16_t)(tick - prev);
        if (step > 1) {
            CHECK(0, "skipped from %u to %u at %llu us", prev, tick,
                  (unsigned long long)micros);
            break;
        }
        if (tick != want(micros)) {
            CHECK(0, "%llu us gave %u, want %u", (unsigned long long)micros, tick,
                  want(micros));
            break;
        }
        prev = tick;
    }
    /* a week of uptime still multiplies without overflow */
    uint64_t week = 7ull * 86400ull * 1000000ull;
    CHECK(fuji_tick_value(week) == want(week), "a week of uptime");

    if (fails) {
        fprintf(stderr, "test_tick: FAIL, %d checks\n", fails);
        return 1;
    }
    printf("test_tick: PASS\n");
    return 0;
}
