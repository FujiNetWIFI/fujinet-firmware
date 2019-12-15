/**
 * PLATOTerm64 - A PLATO Terminal for the Commodore 64
 * Based on Steve Peltz's PAD
 * 
 * Author: Thomas Cherryhomes <thom.cherryhomes at gmail dot com>
 *
 * terminal.c - Terminal state functions
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#include "protocol.h"

/**
 * terminal_init()
 * Initialize terminal state
 */
void terminal_init(void);

/**
 * terminal_initial_position()
 * Set terminal initial position after splash screen.
 */
void terminal_initial_position(void);

/**
 * terminal_set_tty(void) - Switch to TTY mode
 */
void terminal_set_tty(void);

/**
 * terminal_set_plato(void) - Switch to PLATO mode
 */
void terminal_set_plato(void);


/**
 * terminal_char_load - Store a character into the user definable
 * character set.
 */
void terminal_char_load(padWord charnum, charData theChar);

#endif
