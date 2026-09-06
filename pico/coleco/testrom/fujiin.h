/* fujiin.h -- controller input, entirely through OS7.
 *
 * OS7's POLLER does the scanning and debouncing and writes into the cartridge
 * header's controller data area -- the buffer z88dk reserves as
 * os7_bios_controller and os7lib maps as ControllerData. So a client's whole
 * input layer is: call poller() from the vblank NMI, read the struct from the
 * main loop, and turn levels into edges. No port banging, no debounce of our
 * own, no scan table.
 */

#ifndef FUJIIN_H
#define FUJIIN_H

#include <stdbool.h>

/* Edge-triggered events, one per call. */
#define IN_NONE   0
#define IN_UP     1
#define IN_DOWN   2
#define IN_LEFT   3
#define IN_RIGHT  4
#define IN_FIRE   5
#define IN_KEY0   0x10      /* + digit, so IN_KEY0 + 7 is keypad 7 */
#define IN_KEYSTAR 0x1A
#define IN_KEYHASH 0x1B

/* Install the NMI handler (which calls poller() and decays the sound click)
 * and enable both controllers.
 * The handler touches only the BIOS, the VDP ports and RAM -- never the
 * mailbox pages -- which is what makes the vblank NMI safe to leave running
 * through a transaction. */
void in_init(void);

/* One event, or IN_NONE. Auto-repeats a held direction so paging a long
 * directory does not need 40 separate pushes. */
unsigned char in_read(void);

#endif /* FUJIIN_H */
