#include <os7.h>
#include <interrupt.h>
#include <arch/coleco.h>

#include "fujiin.h"
#include "fujisnd.h"

#define JOY_UP    0x01
#define JOY_RIGHT 0x02
#define JOY_DOWN  0x04
#define JOY_LEFT  0x08
#define BTN_FIRE  0x40

/* OS7 reports "no key" as a value outside the keypad's own 0-9, *, # range. */
#define KEY_NONE_MIN 0x0C

#define REPEAT_FIRST 20     /* vblanks before a held direction repeats */
#define REPEAT_NEXT  6      /* vblanks between repeats after that */

static ControllerData *ctl = (ControllerData *)os7_bios_controller;

static unsigned char last_dir;
static unsigned char last_fire;
static unsigned char last_key;
static unsigned char hold;

/* Bumped by the NMI, read by in_read(). The repeat delay has to be counted in
 * VBLANKS, not in calls: the main loop polls in_read() thousands of times a
 * second, so ageing the counter per call would run the auto-repeat at loop
 * speed and fire the cursor across a whole page in one press. (Audible the
 * moment cursor movement got a click -- a single held direction produced a
 * burst of them.) */
static volatile unsigned char frames;
static unsigned char last_frame;

/* The vblank handler. Reading the VDP status register is what clears the
 * interrupt; poller() is what makes the controller struct mean anything. Both
 * live in the BIOS, so this never touches $F800 and up -- the one rule the
 * mailbox needs from an NMI it cannot mask. */
static void nmi(void)
{
    M_PRESERVE_ALL;
    frames++;
    poller();
    snd_tick();
    VDP_STATUS_BYTE = read_register();
    M_RESTORE_ALL;
}

void in_init(void)
{
    ctl->player1_enable = 0xFF;
    ctl->player2_enable = 0xFF;
    add_raster_int(nmi);
    /* VDP register 1: screen on, 16K, vblank interrupt enabled. */
    write_register(0x01, 0xE0);
}

unsigned char in_read(void)
{
    unsigned char now = frames;
    bool vblank = (now != last_frame);
    unsigned char joy = ctl->player1.joystick;
    unsigned char fire = (unsigned char)((ctl->player1.left_button
                                          | ctl->player1.right_button)
                                         & BTN_FIRE);
    unsigned char key = ctl->player1.keyboard;
    unsigned char dir = 0;

    last_frame = now;

    if (joy & JOY_UP)         dir = IN_UP;
    else if (joy & JOY_DOWN)  dir = IN_DOWN;
    else if (joy & JOY_LEFT)  dir = IN_LEFT;
    else if (joy & JOY_RIGHT) dir = IN_RIGHT;

    if (dir != 0) {
        if (dir != last_dir) {
            last_dir = dir;
            hold = REPEAT_FIRST;
            return dir;
        }
        if (vblank && hold != 0 && --hold == 0) {
            hold = REPEAT_NEXT;
            return dir;
        }
    } else {
        last_dir = 0;
    }

    if (fire != 0 && last_fire == 0) {
        last_fire = fire;
        return IN_FIRE;
    }
    if (fire == 0)
        last_fire = 0;

    if (key < KEY_NONE_MIN) {
        if (key != last_key) {
            last_key = key;
            return (unsigned char)(IN_KEY0 + key);
        }
    } else {
        last_key = 0xFF;
    }

    return IN_NONE;
}
