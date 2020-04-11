#include "voice.h"

void sioVoice::sio_write()
{
    byte n = 40;
    byte ck;

    memset(buffer, 0, n); // clear buffer

    ck = sio_to_peripheral(buffer, n);

    if (ck == sio_checksum(buffer, n))
    {
        sio_sam();
        sio_complete();
    }
    else
    {
        sio_error();
    }
}

void sio_sam(){};
