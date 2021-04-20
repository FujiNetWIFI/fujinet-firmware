#ifndef IEC_VOICE_H
#define IEC_VOICE_H

#include <string>
#include "iecBus.h"
#include "iecDevice.h"
#include "samlib.h"

class iecVoice : public iecDevice
{
protected:
    // act like a printer for POC
    uint8_t lastAux1 = 0;
    uint8_t buffer_idx = 0;
    uint8_t sioBuffer[41];
    uint8_t lineBuffer[121];
    uint8_t samBuffer[121];
    void _write();

    void _process(void) override;
    virtual void _status() override;

private:
    bool sing = false;
    std::string pitch;
    std::string mouth;
    bool phonetic = false;
    std::string speed;
    std::string throat;

    void _sam();
    void _sam_parameters();

public:
};

#endif // IEC_VOICE_H