/* tmpl_cart.h -- the template cartridge's pin map and core1 entry.
 *
 *   GP0-GP12   A0-A12    one contiguous mask: offset = pins & ADDR_MASK
 *   GP13       /CE       low while the console reads the window, once per access
 *   GP14-GP21  D0-D7
 *   GP22       LED
 */

#ifndef TMPL_CART_H
#define TMPL_CART_H

#define ADDR_MASK     0x00001FFFu
#define CE_PIN        13
#define CE_MASK       (1u << CE_PIN)
#define D0_PIN        14
#define DATA_MASK     (0xFFu << D0_PIN)
#define LED_PIN       22

#define BUS_GPIO_MASK (ADDR_MASK | CE_MASK | DATA_MASK)

/* /CE falling to the address sample: 50 ns at 200 MHz, for the skew between address
 * lines through the level shifters. */
#define TMPL_SETTLE_CYCLES 10

void tmpl_core1_main(void);

#endif /* TMPL_CART_H */
