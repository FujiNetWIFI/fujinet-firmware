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
    std::optional<std::string> read_tz_from_host(const FujiSIOPacket &packet);

    // set the Config timezone for the whole FujiNet (as in WebUI)
    void set_fn_tz(const FujiSIOPacket &packet);
    void set_alternate_tz(const FujiSIOPacket &packet);

public:
    void sio_process(const FujiSIOPacket &packet) override;
    void sio_status(const FujiSIOPacket &packet) override {};
};

#endif // SIO_CLOCK_H
