/* fujiin.h -- controller input through OS7: the vblank NMI calls poller(), which scans and
 * debounces into os7_bios_controller, and the main loop turns its levels into edges.
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

/* Install the NMI handler (poller() and the sound click) and enable both controllers. It
 * never touches the mailbox pages, so it may run through a transaction. */
void in_init(void);

/* Vblanks since in_init(). Wraps at 256, which is fine for the short waits it
 * is used for -- the splash's minimum hold. */
unsigned char in_frames(void);

/* One event, or IN_NONE. Auto-repeats a held direction so paging a long
 * directory does not need 40 separate pushes. */
unsigned char in_read(void);

#endif /* FUJIIN_H */
