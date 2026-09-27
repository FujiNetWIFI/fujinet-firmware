/* astrocade_cart.h -- pin map and core1 entry for the cartridge bus. */

#ifndef ASTROCADE_CART_H
#define ASTROCADE_CART_H

/* Pin map shared by every fujicade board (boards/fujicade*.h):
 *   GP0-GP12   A0-A12        one contiguous mask, addr = pins & 0x1FFF
 *   GP13       /ENABLE       pre-decoded cart select, active low
 *   GP14-GP21  D0-D7
 *   GP22       self-test trigger (test pad on the PCB)
 *   GP25       LED
 *   GP26       console-5V sense: edge pin 25 through a divider (the PCB;
 *              FUJICADE_VSENSE_PIN in the board header turns gating on)
 *   GP27       debug UART TX (test pad on the PCB)
 */
#define ADDR_MASK   0x00001FFFu
#define EN_PIN      13
#define EN_MASK     (1u << EN_PIN)
#define D0_PIN      14
#define DATA_MASK   (0xFFu << D0_PIN)

#define BUS_GPIO_MASK (ADDR_MASK | EN_MASK | DATA_MASK)

/* The cart serves a read while (pins & SERVE_MASK) == SERVE_WANT: Enable
 * low and -- on boards with console power sense -- the console powered. */
#ifdef FUJICADE_VSENSE_PIN
#define VSENSE_MASK (1u << FUJICADE_VSENSE_PIN)
#else
#define VSENSE_MASK 0u
#endif
#define SERVE_MASK  (EN_MASK | VSENSE_MASK)
#define SERVE_WANT  VSENSE_MASK

void astrocade_core1_main(void);

#endif /* ASTROCADE_CART_H */
