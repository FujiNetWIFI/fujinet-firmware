/**
 * Network Testing tools
 *
 * ncopy - copy files to/from N:
 *
 * Author: Thomas Cherryhomes
 *  <thom.cherryhomes@gmail.com>
 *
 * Released under GPL 3.0
 * See COPYING for details.
 */

#ifndef NSIO_H
#define NSIO_H

void nopen(unsigned char unit, char* buf, unsigned char aux1);
void nclose(unsigned char unit);
void nread(unsigned char unit, char* buf, unsigned short len);
void nwrite(unsigned char unit, char* buf, unsigned short len);
void nstatus(unsigned char unit);

#endif /* NSIO_H */
