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
	Debug_printf("\r\nCLOCK: Sending DIB reply\r\n");
	std::vector<uint8_t> data = create_dib_reply_packet(
		"FN_CLOCK",                                                     // name
		STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE,                 // status
		{ 0, 0, 0 },                                                    // block size
		{ SP_TYPE_BYTE_FUJINET_CLOCK, SP_SUBTYPE_BYTE_FUJINET_CLOCK },  // type, subtype
		{ 0x00, 0x01 }                                                  // version.
	);
	IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data.data(), data.size());
}

void iwmClock::iwm_status(iwm_decoded_cmd_t cmd)
{
    time_t tt;
    struct tm *now;

    uint8_t status_code = get_status_code(cmd);
    Debug_printf("\r\n[CLOCK] Device %02x Status Code %02x\r\n", id(), status_code);

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
    case 'T': // Date and time, easy to be used by general programs
        tt = time(nullptr);
#ifdef ESP_PLATFORM
        setenv("TZ",Config.get_general_timezone().c_str(),1);
#endif
        tzset();
        now = localtime(&tt);

        data_buffer[0] = (now->tm_year)/100 + 19;
        data_buffer[1] = now->tm_year%100;
        data_buffer[2] = now->tm_mon + 1;
        data_buffer[3] = now->tm_mday;
        data_buffer[4] = now->tm_hour;
        data_buffer[5] = now->tm_min;
        data_buffer[6] = now->tm_sec;
        data_len = 7;
        break;
    case 'P': // Date and time, to be used by a ProDOS driver
        tt = time(nullptr);
#ifdef ESP_PLATFORM
        setenv("TZ",Config.get_general_timezone().c_str(),1);
#endif
        tzset();
        now = localtime(&tt);

        // See format in 6.1 of https://prodos8.com/docs/techref/adding-routines-to-prodos/
        data_buffer[0] = now->tm_mday + ((now->tm_mon + 1)<<5);
        data_buffer[1] = ((now->tm_year%100)<<1) + ((now->tm_mon + 1)>>3);
        data_buffer[2] = now->tm_min;
        data_buffer[3] = now->tm_hour;
        data_len = 4;
        break;
    case 'S': // Date and time, ASCII string in SOS set_time format YYYYMMDDxHHMMSSxxx
        tt = time(nullptr);
#ifdef ESP_PLATFORM
        setenv("TZ",Config.get_general_timezone().c_str(),1);
#endif
        tzset();
        now = localtime(&tt);

        data_buffer[0] = (((now->tm_year)/100 + 19) / 10) + 0x30;
        data_buffer[1] = (((now->tm_year)/100 + 19) % 10) + 0x30;
        data_buffer[2] = ((now->tm_year%100) / 10) + 0x30;
        data_buffer[3] = ((now->tm_year%100) % 10) + 0x30;
        data_buffer[4] = ((now->tm_mon + 1)  / 10) + 0x30;
        data_buffer[5] = ((now->tm_mon + 1) % 10) + 0x30;
        data_buffer[6] = (now->tm_mday / 10) + 0x30;
        data_buffer[7] = (now->tm_mday % 10) + 0x30;
        data_buffer[8] = '0';
        data_buffer[9] = (now->tm_hour / 10) + 0x30;
        data_buffer[10] = (now->tm_hour % 10) + 0x30;
        data_buffer[11] = (now->tm_min / 10) + 0x30;
        data_buffer[12] = (now->tm_min %10) + 0x30;
        data_buffer[13] = (now->tm_sec / 10) + 0x30;
        data_buffer[14] = (now->tm_sec % 10) + 0x30;
        data_buffer[15] = '0';
        data_buffer[16] = '0';
        data_buffer[17] = '0';
        
        data_len = 18;
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
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
    case 0x00: // status
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case 0x06: // open
        Debug_printf("\r\nhandling open command");
        iwm_open(cmd);
        break;
    case 0x07: // close
        Debug_printf("\r\nhandling close command");
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