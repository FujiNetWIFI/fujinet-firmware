/**
 * Config for SpartaDOS/DOS XL
 *
 * Author: Thom Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Licensed under GPL v. 3
 * See COPYING for details
 */

#ifndef LIST_H
#define LIST_H

/**
 * List Host Slots
 */
unsigned char list_host_slots(void);

/**
 * List device slots
 */
unsigned char list_device_slots(void);

/**
 * List TNFS directory on host slot
 */
unsigned char list_directory(int argc, char* argv[]);

#endif /* LIST_H */
