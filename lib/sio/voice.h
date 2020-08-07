#ifndef VOICE_H
#define VOICE_H

#include "sio.h"
#include "samlib.h"



class sioVoice : public sioDevice
{
protected:
    // act like a printer for POC
    uint8_t lastAux1 = 0;

    uint8_t buffer[41];
    void sio_write();

    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override;

private:
    void sio_sam();

public:
};

#endif /* VOICE_H */