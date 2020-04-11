#ifndef VOICE_H
#define VOICE_H

#include "sio.h"
#include "samlib.h"

class sioVoice : public sioDevice
{
protected:
    // act like a printer for POC
    byte lastAux1 = 0;

    byte buffer[40];
    void sio_write();

    virtual void sio_process();
    virtual void sio_status();

private:
    void sio_sam();

public:
};

#endif /* VOICE_H */