/**
 * Function to call sio
 */


#ifndef SIO_H
#define SIO_H

#define DEVIC_N                0x70

#define DSTATS_NONE            0x00
#define DSTATS_READ            0x40
#define DSTATS_WRITE           0x80

#define DTIMLO_DEFAULT         0x0F

#define DBYT_NONE              0
#define DBYT_OPEN              256

void _siov();
unsigned char siov(unsigned char ddevic,
		   unsigned char dunit,
		   unsigned char dcomnd,
		   unsigned char dstats,
		   void *dbuf,
		   unsigned short dbyt,
		   unsigned char dtimlo,
		   unsigned char daux1,
		   unsigned char daux2);

#endif
