#ifdef BUILD_RS232

#include "apetime.h"

#include <cstring>
#include <ctime>

#include "fujiCommandID.h"
#include "fnConfig.h"
#include "../../clock/Clock.h"
#include "../../include/debug.h"

std::optional<std::string> rs232ApeTime::_read_tz(const FujiBusPacket &packet)
{
    auto s = packet.dataAsString();
    if (!s || s->empty()) {
        Debug_printv("ERROR: No timezone sent");
        return std::nullopt;
    }
    std::string tz = s.value();
    while (!tz.empty() && tz.back() == '\0')
        tz.pop_back();
    return tz;
}

void rs232ApeTime::_set_alternate_tz(const FujiBusPacket &packet)
{
    transaction_continue(TRANS_STATE::NO_GET);
    auto tz = _read_tz(packet);
    if (tz) {
        alternate_tz = tz.value();
        Debug_printf("Alt TZ set to <%s>\n", alternate_tz.c_str());
    }
    transaction_complete();
}

void rs232ApeTime::_set_fn_tz(const FujiBusPacket &packet)
{
    transaction_continue(TRANS_STATE::NO_GET);
    auto tz = _read_tz(packet);
    if (tz)
        Config.store_general_timezone(tz->c_str());
    transaction_complete();
}

void rs232ApeTime::_get_time_apetime(const FujiBusPacket &packet, bool force_alt)
{
    transaction_continue(TRANS_STATE::NO_GET);
    bool use_alt = force_alt ||
                   (packet.paramCount() > 0 && packet.param(0) == 0x01);
    auto t = Clock::get_current_time_apetime(
        Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
    transaction_put(t.data(), t.size(), false);
}

void rs232ApeTime::_get_time_simple(const FujiBusPacket &packet)
{
    transaction_continue(TRANS_STATE::NO_GET);
    bool use_alt = packet.paramCount() > 0 && packet.param(0) == 0x01;
    auto t = Clock::get_current_time_simple(
        Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
    transaction_put(t.data(), t.size(), false);
}

void rs232ApeTime::_get_time_prodos(const FujiBusPacket &packet)
{
    transaction_continue(TRANS_STATE::NO_GET);
    bool use_alt = packet.paramCount() > 0 && packet.param(0) == 0x01;
    auto t = Clock::get_current_time_prodos(
        Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
    transaction_put(t.data(), t.size(), false);
}

void rs232ApeTime::_get_time_sos(const FujiBusPacket &packet)
{
    transaction_continue(TRANS_STATE::NO_GET);
    bool use_alt = packet.paramCount() > 0 && packet.param(0) == 0x01;
    std::string s = Clock::get_current_time_sos(
        Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
    transaction_put(s.c_str(), s.size() + 1, false);
}

void rs232ApeTime::_get_time_iso_local(const FujiBusPacket &packet)
{
    transaction_continue(TRANS_STATE::NO_GET);
    bool use_alt = packet.paramCount() > 0 && packet.param(0) == 0x01;
    std::string s = Clock::get_current_time_iso(
        Clock::tz_to_use(use_alt, alternate_tz, Config.get_general_timezone()));
    transaction_put(s.c_str(), s.size() + 1, false);
}

void rs232ApeTime::_get_time_iso_utc()
{
    transaction_continue(TRANS_STATE::NO_GET);
    std::string s = Clock::get_current_time_iso("UTC+0");
    transaction_put(s.c_str(), s.size() + 1, false);
}

void rs232ApeTime::_get_general_tz()
{
    transaction_continue(TRANS_STATE::NO_GET);
    const std::string tz = Config.get_general_timezone();
    transaction_put(tz.c_str(), tz.size() + 1, false);
}

void rs232ApeTime::_get_general_tz_len()
{
    transaction_continue(TRANS_STATE::NO_GET);
    uint8_t len = Config.get_general_timezone().size() + 1;
    transaction_put(&len, 1, false);
}

void rs232ApeTime::rs232_process(FujiBusPacket &packet)
{
    switch (packet.command())
    {
    case APETIMECMD_GETTZTIME:
        _get_time_apetime(packet, true);
        break;
    case APETIMECMD_GETTIME:
        _get_time_apetime(packet, false);
        break;
    case APETIMECMD_SETTZ_ALT2:
        _get_time_simple(packet);
        break;
    case APETIMECMD_GET_PRODOS:
        _get_time_prodos(packet);
        break;
    case APETIMECMD_GET_SOS:
        _get_time_sos(packet);
        break;
    case APETIMECMD_GET_ISO_LOCAL:
        _get_time_iso_local(packet);
        break;
    case APETIMECMD_GET_ISO_UTC:
        _get_time_iso_utc();
        break;
    case APETIMECMD_SETTZ:
        _set_alternate_tz(packet);
        break;
    case APETIMECMD_SETTZ_ALT:
        _set_fn_tz(packet);
        break;
    case APETIMECMD_GET_GENERAL:
        _get_general_tz();
        break;
    case APETIMECMD_GETTZ_LEN:
        _get_general_tz_len();
        break;
    default:
        Debug_printv("Unknown rs232 clock cmd: %02x", packet.command());
        transaction_error();
        break;
    }
}

#endif /* BUILD_RS232 */
