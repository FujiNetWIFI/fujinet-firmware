#ifndef SIO_CLOCK_H
#define SIO_CLOCK_H

#include <optional>
#include <string>

#include "bus.h"
#include "../../clock/Clock.h"

class sioClock : public virtualDevice
{
private:
    std::string alternate_tz = "";
    std::optional<std::string> read_tz_from_host();

    // set the Config timezone for the whole FujiNet (as in WebUI)
    void set_fn_tz();
    void set_alternate_tz();

public:
    void sio_process(uint32_t commanddata, uint8_t checksum) override;
    virtual void sio_status() override {};
};

#endif // SIO_CLOCK_H
