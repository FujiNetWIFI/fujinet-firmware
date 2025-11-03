#ifndef VOICE_H
#define VOICE_H

#include <string>

#include "bus.h"
#include "samlib.h"

class sioVoice : public virtualDevice
{
protected:
    // act like a printer for POC
    uint8_t lastAux1 = 0;
    uint8_t buffer_idx = 0;
    uint8_t sioBuffer[41];
    uint8_t lineBuffer[121];
    uint8_t samBuffer[121];
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
//    std::string samplerate;
#ifdef ESP32S3_I2S_OUT
    std::string i2sOut;
#endif

    void sio_sam();
    void sio_sam_parameters();
    void sio_sam_presets(int pr);

public:
};

#endif /* VOICE_H */