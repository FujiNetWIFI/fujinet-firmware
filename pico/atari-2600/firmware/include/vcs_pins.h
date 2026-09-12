/* vcs_pins.h -- how the cartridge connector reaches the RP2040.
 *
 * Kept out of vcs_cart.h because that header is compiled by the MAME device
 * and the host tests, which have no pico-sdk.
 *
 * THE ADDRESS WIRING IS gtortone's PlusCart-Pico, UNCHANGED (see ../include/
 * board.h in this same tree): A0-A11 on GP2-GP13 and A12 on GP14. Keeping it
 * means the UnoCart-2600 cartridge-slot breakout and the same ribbon work
 * as-is, and one shift extracts the whole address.
 *
 * THE DATA WIRING IS NOT. PlusCart puts D0-D7 on GP22-GP29, which needs the
 * "purple" YD-RP2040 board because a stock Pico does not bring GP23-25 out at
 * all -- and, more seriously, it wires the RP2040 straight to a 5V bus. RP2040
 * GPIOs are not 5V tolerant; that board survives on its clamp diodes and the
 * NMOS bus's weak highs. UnoCart used a 5V-tolerant STM32 and did not have the
 * problem. So D0-D7 move to GP15-GP22, still contiguous, and a 74LVC8T245 with
 * its direction on GP26 does the level shifting.
 *
 * This is a deliberate departure from proven hardware and is the port's main
 * hardware risk: gtortone's existing board will not run this firmware without
 * rewiring the data byte.
 */
#ifndef VCS_PINS_H
#define VCS_PINS_H

#define ADDR_PIN    2u          /* A0-A12 on GP2-GP14 */
#define ADDR_BITS   13u
#define ADDR_MASK   (0x1FFFu << ADDR_PIN)

#define D0_PIN      15u         /* D0-D7 on GP15-GP22, through a '245 */
#define DATA_MASK   (0xFFu << D0_PIN)

#define DIR_PIN     26u         /* '245 direction: 0 = cart drives the console */
#define DIR_MASK    (1u << DIR_PIN)

#define BUS_GPIO_MASK (ADDR_MASK | DATA_MASK)

/* The bus, through SIO, which is the only path fast enough. The address and
 * data fields are each contiguous, so each is one shift. */
#define ADDR_IN     ((sio_hw->gpio_in & ADDR_MASK) >> ADDR_PIN)
#define DATA_IN     ((sio_hw->gpio_in & DATA_MASK) >> D0_PIN)

/* Drive the data byte with a single store: XOR against the current output and
 * write gpio_togl, so only the eight data pins change and nothing else on the
 * port is disturbed. PlusCart's idiom, and it is the right one. */
#define DATA_OUT(v) (sio_hw->gpio_togl = \
        (sio_hw->gpio_out ^ ((uint32_t)(v) << D0_PIN)) & DATA_MASK)

#define DATA_DRIVE  (sio_hw->gpio_oe_set = DATA_MASK)
#define DATA_RELEASE (sio_hw->gpio_oe_clr = DATA_MASK)

#endif /* VCS_PINS_H */
