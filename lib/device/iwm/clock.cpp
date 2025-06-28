#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "clock.h"

#include "fnConfig.h"
#include "../hardware/led.h"

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

void iwmClock::send_status_reply_packet()
{
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    IWM.iwm_send_packet(id(),iwm_packet_type_t::status,SP_ERR_NOERROR, data, 4);
}

void iwmClock::send_status_dib_reply_packet()
{
	Debug_printf("CLOCK: Sending DIB reply\r\n");
	std::vector<uint8_t> data = create_dib_reply_packet(
		"FN_CLOCK",                                                     // name
		STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE,                 // status
		{ 0, 0, 0 },                                                    // block size
		{ SP_TYPE_BYTE_FUJINET_CLOCK, SP_SUBTYPE_BYTE_FUJINET_CLOCK },  // type, subtype
		{ 0x00, 0x01 }                                                  // version.
	);
	IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data.data(), data.size());
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
    uint8_t control_code = get_status_code(cmd);
#ifdef DEBUG
    auto as_char = (char) control_code;
    Debug_printf("[CLOCK] Device %02x Control Code %02x('%c')\r\n", id(), control_code, isprint(as_char) ? as_char : '.');
#endif

    IWM.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);

    uint8_t err_result = SP_ERR_NOERROR;

    switch (control_code)
    {
        case 'T':
            set_tz();
            break;
        case 't':
            set_alternate_tz();
            break;
        default:
            err_result = SP_ERR_BADCTL;
            break;
    }

    send_reply_packet(err_result);
}

void iwmClock::iwm_status(iwm_decoded_cmd_t cmd)
{
    uint8_t status_code = get_status_code(cmd);
    bool use_alternate_tz = false;

#ifdef DEBUG
    auto as_char = (char) status_code;
    Debug_printf("[CLOCK] Device %02x Status Code %02x('%c')\r\n", id(), status_code, isprint(as_char) ? as_char : '.');
#endif
    switch (status_code)
    {
    case IWM_STATUS_STATUS: // 0x00
        send_status_reply_packet();
        return;
        break;
    case IWM_STATUS_DIB: // 0x03
        send_status_dib_reply_packet();
        return;
        break;

    // Uppercase = use FN tz, otherwise use alt tz
    case 'T':
    case 't': {
        use_alternate_tz = status_code == 't';
        // Date and time, easy to be used by general programs
        auto simpleTime = Clock::get_current_time_simple(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(simpleTime.begin(), simpleTime.end(), data_buffer);
        data_len = simpleTime.size();
        break;
    }
    case 'P':
    case 'p': {
        use_alternate_tz = status_code == 'p';
        // Date and time, to be used by a ProDOS driver
        auto prodosTime = Clock::get_current_time_prodos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(prodosTime.begin(), prodosTime.end(), data_buffer);
        data_len = prodosTime.size();
        break;
    }
    case 'S':
    case 's': {
        use_alternate_tz = status_code == 's';
        // Date and time, ASCII string in Apple /// SOS format: YYYYMMDD0HHMMSS000
        std::string sosTime = Clock::get_current_time_sos(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(sosTime.begin(), sosTime.end(), data_buffer);
        data_buffer[sosTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = sosTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case 'I':
    case 'i': {
        use_alternate_tz = status_code == 'i';
        // Date and time, ASCII string in ISO format
        std::string utcTime = Clock::get_current_time_iso(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(utcTime.begin(), utcTime.end(), data_buffer);
        data_buffer[utcTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = utcTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case 'Z':
    case 'z': {
        use_alternate_tz = status_code == 'z';
        // utc (zulu)
        std::string isoTime = Clock::get_current_time_iso("UTC+0");
        std::copy(isoTime.begin(), isoTime.end(), data_buffer);
        data_buffer[isoTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = isoTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case 'A':
    case 'a': {
        use_alternate_tz = status_code == 'a';
        // Apetime (Atari, but why not eh?) with TZ
        auto apeTime = Clock::get_current_time_apetime(Clock::tz_to_use(use_alternate_tz, alternate_tz, Config.get_general_timezone()));
        std::copy(apeTime.begin(), apeTime.end(), data_buffer);
        data_len = apeTime.size();
        break;
    }
    case 'G': {
        // Get current system timezone
        std::string curr = Config.get_general_timezone();
        std::copy(curr.begin(), curr.end(), data_buffer);
        data_buffer[curr.size()] = '\0';
        data_len = curr.size() + 1;
        break;
    }
    default:
        send_reply_packet(SP_ERR_BADCTL);
        return;
    }

    // If we got here, we have data to send
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR_NOERROR, data_buffer, data_len);
}

void iwmClock::iwm_open(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\nClock: Open\n");
    send_reply_packet(SP_ERR_NOERROR);
}

void iwmClock::iwm_close(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\nClock: Close\n");
    send_reply_packet(SP_ERR_NOERROR);
}


void iwmClock::process(iwm_decoded_cmd_t cmd)
{
    fnLedManager.set(LED_BUS, true);
    switch (cmd.command)
    {
    case SP_CMD_STATUS:
        Debug_printf("\r\nclock: handling status command\r\n");
        iwm_status(cmd);
        break;
    case SP_CMD_CONTROL:
        Debug_printf("\r\nclock: handling control command");
        iwm_ctrl(cmd);
        break;
    case SP_CMD_OPEN:
        Debug_printf("\r\nclock: handling open command");
        iwm_open(cmd);
        break;
    case SP_CMD_CLOSE:
        Debug_printf("\r\nclock: handling close command");
        iwm_close(cmd);
        break;
    default:
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
    fnLedManager.set(LED_BUS, false);
}

void iwmClock::shutdown()
{
}



#endif /* BUILD_APPLE */
