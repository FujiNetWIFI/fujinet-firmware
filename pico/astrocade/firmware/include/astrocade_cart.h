/* astrocade_cart.h -- pin map and core1 entry for the cartridge bus. */

#ifndef ASTROCADE_CART_H
#define ASTROCADE_CART_H

/* Plain Pico assignments (see boards/fujicade.h for the board story):
 *   GP0-GP12   A0-A12        one contiguous mask, addr = pins & 0x1FFF
 *   GP13       /ENABLE       pre-decoded cart select, active low
 *   GP14-GP21  D0-D7
 *   GP22       (spare: future self-test trigger)
 *   GP25       LED
 *   GP26       (spare: future console-5V power sense, cart pin 20 divider)
 *   GP27      (spare: future debug UART TX)
 */
#define ADDR_MASK   0x00001FFFu
#define EN_PIN      13
#define EN_MASK     (1u << EN_PIN)
#define D0_PIN      14
#define DATA_MASK   (0xFFu << D0_PIN)

#define BUS_GPIO_MASK (ADDR_MASK | EN_MASK | DATA_MASK)

void astrocade_core1_main(void);

#endif /* ASTROCADE_CART_H */
