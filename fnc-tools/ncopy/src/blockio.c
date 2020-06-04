/**
 * Simple CIO GETCHR/PUTCHR wrappers
 */

#include <atari.h>
#include "blockio.h"
#include "cio.h"

void open(unsigned char channel, unsigned char aux1, char* buf, unsigned short len)
{
  OS.iocb[channel].buffer=buf;
  OS.iocb[channel].buflen=len;
  OS.iocb[channel].command=IOCB_OPEN;
  OS.iocb[channel].aux1=aux1;
  dciov(channel);
}

void close(unsigned char channel)
{
  OS.iocb[channel].command=IOCB_CLOSE;
  dciov(channel);
}

void get(unsigned char channel, char* buf, unsigned short len)
{
  OS.iocb[channel].buffer=buf;
  OS.iocb[channel].buflen=len;
  OS.iocb[channel].command=IOCB_GETCHR;
  dciov(channel);
}

void put(unsigned char channel, char* buf, unsigned short len)
{
  OS.iocb[channel].buffer=buf;
  OS.iocb[channel].buflen=len;
  OS.iocb[channel].command=IOCB_PUTCHR;
  dciov(channel);
}
