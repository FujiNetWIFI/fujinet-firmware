/**
 * Simple CIO GETCHR/PUTCHR wrappers
 */

#include <atari.h>
#include "blockio.h"
#include "cio.h"

void open(char* buf, unsigned short len, unsigned char aux1)
{
  OS.iocb[2].buffer=buf;
  OS.iocb[2].buflen=len;
  OS.iocb[2].command=IOCB_OPEN;
  OS.iocb[2].aux1=aux1;
  dciov();
}

void close(void)
{
  OS.iocb[2].command=IOCB_CLOSE;
  dciov();
}

void get(char* buf, unsigned short len)
{
  OS.iocb[2].buffer=buf;
  OS.iocb[2].buflen=len;
  OS.iocb[2].command=IOCB_GETCHR;
  dciov();
}

void put(char* buf, unsigned short len)
{
  OS.iocb[2].buffer=buf;
  OS.iocb[2].buflen=len;
  OS.iocb[2].command=IOCB_PUTCHR;
  dciov();
}
