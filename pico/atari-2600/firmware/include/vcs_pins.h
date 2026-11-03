/* vcs_pins.h -- how the cartridge connector reaches the RP2354A (Fujiversal-Atari2600 Rev1).
 *
 * Kept out of vcs_cart.h because that header is compiled by the MAME device and the host
 * tests, which have no pico-sdk.
 *
 * The edge goes straight to the RP: its GPIO0-25 are 5 V-tolerant while IOVDD is up, and a
 * 3.3 V high clears the 6507's TTL VIH. No buffers and no direction pin: the per-access
 * output enable is the only direction control. The address wiring is gtortone's
 * PlusCart-Pico (../include/board.h); D0-D7 sit on GP15-GP22, still one shift.
 */

#ifndef VCS_PINS_H
#define VCS_PINS_H

#define ADDR_PIN    2u          /* A0-A12 on GP2-GP14 */
#define ADDR_BITS   13u
#define ADDR_MASK   (0x1FFFu << ADDR_PIN)

#define D0_PIN      15u         /* D0-D7 on GP15-GP22 */
#define DATA_MASK   (0xFFu << D0_PIN)

#define LED_PIN     25u
#define VSENSE_PIN  27u         /* the console's +5 V through 100k/150k: high while it is on */

#define BUS_GPIO_MASK (ADDR_MASK | DATA_MASK)

/* The bus, through SIO, which is the only path fast enough. The address and
 * data fields are each contiguous, so each is one shift. */
#define ADDR_IN     ((sio_hw->gpio_in & ADDR_MASK) >> ADDR_PIN)
#define DATA_IN     ((sio_hw->gpio_in & DATA_MASK) >> D0_PIN)
#define CONSOLE_ON  ((sio_hw->gpio_in >> VSENSE_PIN) & 1u)

/* Drive the data byte with a single store: XOR against the current output and
 * write gpio_togl, so only the eight data pins change and nothing else on the
 * port is disturbed. PlusCart's idiom, and it is the right one. */
#define DATA_OUT(v) (sio_hw->gpio_togl = \
        (sio_hw->gpio_out ^ ((uint32_t)(v) << D0_PIN)) & DATA_MASK)
#define DATA_DRIVE  (sio_hw->gpio_oe_set = DATA_MASK)
#define DATA_RELEASE (sio_hw->gpio_oe_clr = DATA_MASK)

#endif /* VCS_PINS_H */
