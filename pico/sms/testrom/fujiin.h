/* fujiin.h -- the joypad in port 1 and the Reset button. */

#ifndef FUJIIN_H
#define FUJIIN_H

#define IN_UP     0x01
#define IN_DOWN   0x02
#define IN_LEFT   0x04
#define IN_RIGHT  0x08
#define IN_1      0x10
#define IN_2      0x20
#define IN_RESET  0x40              /* SMS1 Reset button, from port $DD */

/* Buttons held now, active high. */
unsigned char in_pad(void);

/* Wait for a press and its release; returns the button. */
unsigned char in_wait(void);

#endif /* FUJIIN_H */
