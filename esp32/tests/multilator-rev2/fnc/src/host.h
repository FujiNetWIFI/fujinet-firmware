/**
 * Config for SpartaDOS/DOS XL
 *
 * Author: Thom Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v. 3
 * See COPYING for details
 */

#ifndef HOST_H
#define HOST_H

/**
 * Mount a host slot
 */
void host_mount(unsigned char c);

/**
 * Read host slots
 */
void host_read(void);

/**
 * Host Slot Config
 */
unsigned char host(int argc, char* argv[]);

#endif /* HOST_H */
