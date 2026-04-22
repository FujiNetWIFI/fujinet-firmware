#ifndef APETIME_H
#define APETIME_H

#include <optional>
#include <string>

#include "bus.h"

class rs232ApeTime : public virtualDevice
{
private:
    std::string alternate_tz;

    std::optional<std::string> _read_tz(const FujiBusPacket &packet);
    void _set_alternate_tz(const FujiBusPacket &packet);
    void _set_fn_tz(const FujiBusPacket &packet);
    void _get_time_apetime(const FujiBusPacket &packet, bool force_alt);
    void _get_time_simple(const FujiBusPacket &packet);
    void _get_time_prodos(const FujiBusPacket &packet);
    void _get_time_sos(const FujiBusPacket &packet);
    void _get_time_iso_local(const FujiBusPacket &packet);
    void _get_time_iso_utc();
    void _get_general_tz();
    void _get_general_tz_len();

public:
    void rs232_process(FujiBusPacket &packet) override;
    void rs232_status(FujiStatusReq reqType) override {}
};

#endif // APETIME_H
