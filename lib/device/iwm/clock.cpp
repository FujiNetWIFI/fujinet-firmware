#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "clock.h"
#include "fnConfig.h"

iwmClock::iwmClock()
{
}

iwm_device_status_block_t iwmClock::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  status.block_size = 0;
  return status;
}

iwm_device_info_block_t iwmClock::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  strcpy(dib.name, "FN_CLOCK");
  dib.name_len = strlen(dib.name);
  dib.type = SP_TYPE_BYTE_FUJINET_CLOCK;
  dib.subtype = SP_SUBTYPE_BYTE_FUJINET_CLOCK;
  dib.version = 0x0100;

  return dib;
}

void iwmClock::set_tz()
{
    std::string buffer(data_len, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(buffer.data(), buffer.size());
    Config.store_general_timezone(buffer.c_str());
    Config.save();
    Debug_printf("sys_tz set to: >%s<\n", Config.get_general_timezone().c_str());
    SYSTEM_BUS.transaction_success();
}

void iwmClock::set_alternate_tz()
{
    std::string buffer(data_len, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(buffer.data(), buffer.size());
    alternate_tz = buffer;
    Debug_printf("alt_tz set to: >%s<\n", alternate_tz.c_str());
    SYSTEM_BUS.transaction_success();
}

void iwmClock::iwm_ctrl(const iwm_decoded_cmd_t &cmd)
{
#ifdef DEBUG
    Debug_printf("[CLOCK] Device %02x Control Code %02x('%c')\r\n", id(), cmd.control_status.fuji.command, isprint(cmd.control_status.fuji.command) ? (char) cmd.control_status.fuji.command : '.');
#endif

    switch (cmd.control_status.fuji.command)
    {
    case APETIMECMD_SETTZ_ALT2:
        set_tz();
        break;
    case APETIMECMD_SETTZ_ALT:
        set_alternate_tz();
        break;
    default:
        SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
        break;
    }
}

void iwmClock::iwm_status(const iwm_decoded_cmd_t &cmd)
{
    bool use_alternate_tz = false;

#ifdef DEBUG
    Debug_printf("[CLOCK] Device %02x Status Code %02x('%c')\r\n", id(), cmd.control_status.fuji.command, isprint(cmd.control_status.fuji.command) ? (char)cmd.control_status.fuji.command : '.');
#endif
    switch (cmd.control_status.fuji.command)
    {
    // Uppercase = use FN tz, otherwise use alt tz
    case APETIMECMD_SETTZ_ALT2:
    case APETIMECMD_SETTZ_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_SETTZ_ALT;
        // Date and time, easy to be used by general programs
        auto simpleTime = Clock::get_current_time_simple(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(simpleTime);
        break;
    }
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS: {
        auto milliTime = Clock::get_current_time_simple_hundredths(Clock::tz_to_use(false, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(milliTime);
        break;
    }
    case APETIMECMD_GET_PRODOS:
    case APETIMECMD_GET_PRODOS_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_PRODOS_ALT;
        // Date and time, to be used by a ProDOS driver
        auto prodosTime = Clock::get_current_time_prodos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(prodosTime);
        break;
    }
    case APETIMECMD_GET_SOS:
    case APETIMECMD_GET_SOS_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_SOS_ALT;
        // Date and time, ASCII string in Apple /// SOS format: YYYYMMDD0HHMMSS000
        std::string sosTime = Clock::get_current_time_sos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(sosTime);
        break;
    }
    case APETIMECMD_GET_ISO_LOCAL:
    case APETIMECMD_GET_ISO_LOCAL_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_ISO_LOCAL_ALT;
        // Date and time, ASCII string in ISO format
        std::string utcTime = Clock::get_current_time_iso(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(utcTime);
        break;
    }
    case APETIMECMD_GET_ISO_UTC:
    case APETIMECMD_GET_ISO_UTC_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_ISO_UTC_ALT;
        // utc (zulu)
        std::string isoTime = Clock::get_current_time_iso("UTC+0");
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(isoTime);
        break;
    }
    case APETIMECMD_GET_ATARI:
    case APETIMECMD_GET_ATARI_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_ATARI_ALT;
        // Apetime (Atari, but why not eh?) with TZ
        auto apeTime = Clock::get_current_time_apetime(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(apeTime);
        break;
    }
    case APETIMECMD_GET_GENERAL: {
        // Get current system timezone
        std::string curr = Config.get_general_timezone();
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(curr);
        break;
    }
    default:
        SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
        return;
    }
}

void iwmClock::iwm_open(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\nClock: Open\n");
    SYSTEM_BUS.transaction_error(SP_ERR::NOERROR);
}

void iwmClock::iwm_close(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\nClock: Close\n");
    SYSTEM_BUS.transaction_error(SP_ERR::NOERROR);
}

void iwmClock::shutdown()
{
}

#endif /* BUILD_APPLE */
