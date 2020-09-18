#ifndef VOICE_H
#define VOICE_H

#include <string>
#include "sio.h"
#include "samlib.h"

class sioVoice : public sioDevice
{
protected:
    // act like a printer for POC
    uint8_t lastAux1 = 0;

    uint8_t sioBuffer[41];
    uint8_t samBuffer[41];
    void sio_write();

    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override;

private:
    bool sing = false;
    std::string pitch;
    std::string mouth;
    bool phonetic = false;
    std::string speed;
    std::string throat;

    void sio_sam();
    void sio_sam_parameters();

public:
};

#endif /* VOICE_H */