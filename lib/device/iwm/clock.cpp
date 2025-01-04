#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "clock.h"

#include "fnConfig.h"
#include "../hardware/led.h"

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
    if (data_len <= 0) return;

    // Ensure there's room for a null terminator if one is not already present
    if (data_buffer[data_len - 1] != '\0' && data_len < MAX_DATA_LEN) {
        data_buffer[data_len] = '\0';
    } else if (data_len == MAX_DATA_LEN && data_buffer[MAX_DATA_LEN - 1] != '\0') {
        // If the buffer is full and the last character is not a null terminator,
        // safely truncate the string to make room for a null terminator.
        data_buffer[MAX_DATA_LEN - 1] = '\0';
    }

    Config.store_general_timezone(reinterpret_cast<const char*>(data_buffer));
}

void iwmClock::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    uint8_t control_code = get_status_code(cmd);
#ifdef DEBUG
    auto as_char = (char) control_code;
    Debug_printf("[CLOCK] Device %02x Control Code %02x('%c')\r\n", id(), control_code, isprint(as_char) ? as_char : '.');
#endif

    IWM.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);

    switch (control_code)
    {
        case 'T':
            set_tz();
            break;
        default:
            break;
    }

}

void iwmClock::iwm_status(iwm_decoded_cmd_t cmd)
{
    uint8_t status_code = get_status_code(cmd);
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
    case 'T': {
        // Date and time, easy to be used by general programs
        auto simpleTime = Clock::get_current_time_simple(Config.get_general_timezone());
        std::copy(simpleTime.begin(), simpleTime.end(), data_buffer);
        data_len = simpleTime.size();
        break;
    }
    case 'P': {
        // Date and time, to be used by a ProDOS driver
        auto prodosTime = Clock::get_current_time_prodos(Config.get_general_timezone());
        std::copy(prodosTime.begin(), prodosTime.end(), data_buffer);
        data_len = prodosTime.size();
        break;
    }
    case 'S': {
        // Date and time, ASCII string in Apple /// SOS format: YYYYMMDD0HHMMSS000
        std::string sosTime = Clock::get_current_time_sos(Config.get_general_timezone());
        std::copy(sosTime.begin(), sosTime.end(), data_buffer);
        data_buffer[sosTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it (this is also a change to the original that sent the char bytes without a null)
        data_len = sosTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case 'I': {
        // Date and time, ASCII string in ISO format
        std::string utcTime = Clock::get_current_time_iso(Config.get_general_timezone());
        std::copy(utcTime.begin(), utcTime.end(), data_buffer);
        data_buffer[utcTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it (this is also a change to the original that sent the char bytes without a null)
        data_len = utcTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case 'Z': {
        // utc (zulu)
        std::string isoTime = Clock::get_current_time_iso("UTC+0");
        std::copy(isoTime.begin(), isoTime.end(), data_buffer);
        data_buffer[isoTime.size()] = '\0';         // this is a string in a buffer, we will null terminate it
        data_len = isoTime.size() + 1;              // and ensure the size reflects the null terminator
        break;
    }
    case 'A': {
        // Apetime (Atari, but why not eh?) with TZ
        auto apeTime = Clock::get_current_time_apetime(Config.get_general_timezone());
        std::copy(apeTime.begin(), apeTime.end(), data_buffer);
        data_len = apeTime.size();
        break;
    }
    case 'B': {
        // Apetime (Atari, but why not eh?) UTC
        auto apeTime = Clock::get_current_time_apetime("UTC+0");
        std::copy(apeTime.begin(), apeTime.end(), data_buffer);
        data_len = apeTime.size();
        break;
    }
    case 'G': {
        // Get current system timezone
        std::string curr = Config.get_general_timezone();
        std::copy(curr.begin(), curr.end(), data_buffer);
        data_len = curr.size();
        break;
    }
    }

    // Debug_printf("Clock: Status code complete, sending response\r\n");
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
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
