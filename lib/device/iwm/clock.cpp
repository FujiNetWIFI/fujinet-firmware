#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "clock.h"

#include "fnConfig.h"

namespace {
    // Helper function to prepare timezone buffer with null termination
    void prepare_tz_buffer(uint8_t* buffer, int& len) {
        if (len <= 0) return;

        // Ensure there's room for a null terminator if one is not already present.
        if (buffer[len - 1] != '\0' && len < MAX_DATA_LEN) {
            buffer[len] = '\0';
        } else if (len == MAX_DATA_LEN && buffer[MAX_DATA_LEN - 1] != '\0') {
            // If the buffer is full and the last character is not a null terminator,
            // safely truncate the string to make room for a null terminator. this should never happen, as it means user sent a 767 byte timezone.
            buffer[MAX_DATA_LEN - 1] = '\0';
        }
    }
}

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
    prepare_tz_buffer(data_buffer, data_len);
    Config.store_general_timezone(reinterpret_cast<const char*>(data_buffer));
    Config.save();
    Debug_printf("sys_tz set to: >%s<\n", Config.get_general_timezone().c_str());
}

void iwmClock::set_alternate_tz()
{
    prepare_tz_buffer(data_buffer, data_len);
    alternate_tz = std::string(reinterpret_cast<const char*>(data_buffer), data_len);
    Debug_printf("alt_tz set to: >%s<\n", alternate_tz.c_str());
}

void iwmClock::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
#ifdef DEBUG
    Debug_printf("[CLOCK] Device %02x Control Code %02x('%c')\r\n", id(), cmd.control_status.fuji.command, isprint(cmd.control_status.fuji.command) ? (char) cmd.control_status.fuji.command : '.');
#endif

    spError_t err_result = SP_ERR::NOERROR;

    switch (cmd.control_status.fuji.command)
    {
        case APETIMECMD_SETTZ_ALT2:
            set_tz();
            break;
        case APETIMECMD_SETTZ_ALT:
            set_alternate_tz();
            break;
        default:
            err_result = SP_ERR::BADCTL;
            break;
    }

    send_reply_packet(err_result);
}

void iwmClock::iwm_status(iwm_decoded_cmd_t cmd)
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
        std::copy(simpleTime.begin(), simpleTime.end(), data_buffer);
        data_len = simpleTime.size();
        break;
    }
    case APETIMECMD_GET_SIMPLE_HUNDREDTHS: {
        auto milliTime = Clock::get_current_time_simple_hundredths(Clock::tz_to_use(false, alternate_tz, Config.get_general_timezone()));
        std::copy(milliTime.begin(), milliTime.end(), data_buffer);
        data_len = milliTime.size();
        break;
    }
    case APETIMECMD_GET_PRODOS:
    case APETIMECMD_GET_PRODOS_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_PRODOS_ALT;
        // Date and time, to be used by a ProDOS driver
        auto prodosTime = Clock::get_current_time_prodos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(prodosTime.begin(), prodosTime.end(), data_buffer);
        data_len = prodosTime.size();
        break;
    }
    case APETIMECMD_GET_SOS:
    case APETIMECMD_GET_SOS_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_SOS_ALT;
        // Date and time, ASCII string in Apple /// SOS format: YYYYMMDD0HHMMSS000
        std::string sosTime = Clock::get_current_time_sos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(sosTime.begin(), sosTime.end(), data_buffer);
        data_buffer[sosTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = sosTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case APETIMECMD_GET_ISO_LOCAL:
    case APETIMECMD_GET_ISO_LOCAL_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_ISO_LOCAL_ALT;
        // Date and time, ASCII string in ISO format
        std::string utcTime = Clock::get_current_time_iso(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(utcTime.begin(), utcTime.end(), data_buffer);
        data_buffer[utcTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = utcTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case APETIMECMD_GET_ISO_UTC:
    case APETIMECMD_GET_ISO_UTC_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_ISO_UTC_ALT;
        // utc (zulu)
        std::string isoTime = Clock::get_current_time_iso("UTC+0");
        std::copy(isoTime.begin(), isoTime.end(), data_buffer);
        data_buffer[isoTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = isoTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case APETIMECMD_GET_ATARI:
    case APETIMECMD_GET_ATARI_ALT: {
        use_alternate_tz = cmd.control_status.fuji.command == APETIMECMD_GET_ATARI_ALT;
        // Apetime (Atari, but why not eh?) with TZ
        auto apeTime = Clock::get_current_time_apetime(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(apeTime.begin(), apeTime.end(), data_buffer);
        data_len = apeTime.size();
        break;
    }
    case APETIMECMD_GET_GENERAL: {
        // Get current system timezone
        std::string curr = Config.get_general_timezone();
        std::copy(curr.begin(), curr.end(), data_buffer);
        data_buffer[curr.size()] = '\0';
        data_len = curr.size() + 1;
        break;
    }
    default:
        send_reply_packet(SP_ERR::BADCTL);
        return;
    }

    // If we got here, we have data to send
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR, data_buffer, data_len);
}

void iwmClock::iwm_open(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\nClock: Open\n");
    send_reply_packet(SP_ERR::NOERROR);
}

void iwmClock::iwm_close(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\nClock: Close\n");
    send_reply_packet(SP_ERR::NOERROR);
}

void iwmClock::shutdown()
{
}

#endif /* BUILD_APPLE */
