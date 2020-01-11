/**
 * Config for SpartaDOS/DOS XL
 *
 * Author: Thom Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v. 3
 * See COPYING for details
 */

#ifndef DISK_H
#define DISK_H

/**
 * Read Device Slots
 */
void disk_read(void);

/**
 * Write device slots
 */
void disk_write(void);

/**
 * Mount disk in device slot
 */
unsigned char disk_mount(int argc, char* argv[]);

/**
 * Eject disk in device slot
 */
unsigned char disk_eject(int argc, char* argv[]);

#endif /* DISK_H */
