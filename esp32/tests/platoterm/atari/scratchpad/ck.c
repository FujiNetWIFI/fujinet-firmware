#include <stdio.h>

/**
   calculate 8-bit checksum.
*/
unsigned char sio_checksum(unsigned char* chunk, int length)
{
  int chkSum = 0;
  for (int i = 0; i < length; i++) {
    chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
  }
  return (unsigned char)chkSum;
}

int main(int argc, char* argv[])
{
  printf("%02x",sio_checksum("IRATA.ONLINE:8005",17));
  return 0;
}
