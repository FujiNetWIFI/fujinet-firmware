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
    uint8_t data[25];

    //* write data buffer first (25 bytes) 3 grp7 + 4 odds
    // General Status byte
    // Bit 7: Block  device
    // Bit 6: Write allowed
    // Bit 5: Read allowed
    // Bit 4: Device online or disk in drive
    // Bit 3: Format allowed
    // Bit 2: Media write protected (block devices only)
    // Bit 1: Currently interrupting (//c only)
    // Bit 0: Currently open (char devices only)
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0;    // block size 1
    data[2] = 0;    // block size 2
    data[3] = 0;    // block size 3
    data[4] = 0x08; // ID string length - 11 chars
    data[5] = 'F';
    data[6] = 'N';
    data[7] = '_';
    data[8] = 'C';
    data[9] = 'L';
    data[10] = 'O';
    data[11] = 'C';
    data[12] = 'K';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';                           // ID string (16 chars total)
    data[21] = SP_TYPE_BYTE_FUJINET_CLOCK;    // Device type    - 0x13  clock
    data[22] = SP_SUBTYPE_BYTE_FUJINET_CLOCK; // Device Subtype - 0x00
    data[23] = 0x00;                          // Firmware version 2 bytes
    data[24] = 0x01;                          //
    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

void iwmClock::iwm_status(iwm_decoded_cmd_t cmd)
{
    time_t tt;
    struct tm *now;

    uint8_t status_code = get_status_code(cmd); 
    Debug_printf("\r\nDevice %02x Status Code %02x\n", id(), status_code);

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
        setenv("TZ",Config.get_general_timezone().c_str(),1);
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
        setenv("TZ",Config.get_general_timezone().c_str(),1);
        tzset();
        now = localtime(&tt);

        // See format in 6.1 of https://prodos8.com/docs/techref/adding-routines-to-prodos/
        data_buffer[0] = now->tm_mday + ((now->tm_mon + 1)<<5);
        data_buffer[1] = ((now->tm_year%100)<<1) + ((now->tm_mon + 1)>>3);
        data_buffer[2] = now->tm_min;
        data_buffer[3] = now->tm_hour;
        data_len = 4;
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
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
    default:
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
    fnLedManager.set(LED_BUS, false);
}

void iwmClock::shutdown()
{
}



#endif /* BUILD_APPLE2 */