/**
 * FujiNet Configurator
 *
 * Functions to display a selection bar
 */

#ifndef BAR_H
#define BAR_H

/**
 * Set up all the registers to display the bar
 */
extern void bar_setup_regs(void);

/**
 * Clear bar from screen
 */
void bar_clear(void);

/**
 * Show bar at Y position
 */
void bar_show(unsigned char y);

/**
 * Set bar color
 */
void bar_set_color(unsigned char c);

#endif /* BAR_H */
