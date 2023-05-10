#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "cpm.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnWiFi.h"
#include "fuji.h"
#include "fnFS.h"
#include "fnFsSD.h"

#include "../hardware/led.h"

#include "../runcpm/abstraction_fujinet_apple2.h"

#include "../runcpm/globals.h"
#include "../runcpm/ram.h"     // ram.h - Implements the RAM
#include "../runcpm/console.h" // console.h - implements console.
#include "../runcpm/cpu.h"     // cpu.h - Implements the emulated CPU
#include "../runcpm/disk.h"    // disk.h - Defines all the disk access abstraction functions
#include "../runcpm/host.h"    // host.h - Custom host-specific BDOS call
#include "../runcpm/cpm.h"     // cpm.h - Defines the CPM structures and calls
#ifdef CCP_INTERNAL
#include "../runcpm/ccp.h" // ccp.h - Defines a simple internal CCP
#endif

#define CPM_TASK_PRIORITY 20

static void cpmTask(void *arg)
{
    Debug_printf("cpmTask()\n");
    while (1)
    {
        Status = Debug = 0;
        Break = Step = -1;
        RAM = (uint8_t *)malloc(MEMSIZE);
        memset(RAM, 0, MEMSIZE);
        memset(filename, 0, sizeof(filename));
        memset(newname, 0, sizeof(newname));
        memset(fcbname, 0, sizeof(fcbname));
        memset(pattern, 0, sizeof(pattern));
        vTaskDelay(100);
        _puts(CCPHEAD);
        _PatchCPM();
        _ccp();
    }
}

iwmCPM::iwmCPM()
{
    rxq = xQueueCreate(2048, sizeof(char));
    txq = xQueueCreate(2048, sizeof(char));
}

void iwmCPM::send_status_reply_packet()
{
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 4);
}

void iwmCPM::send_status_dib_reply_packet()
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
    data[4] = 0x03; // ID string length - 11 chars
    data[5] = 'C';
    data[6] = 'P';
    data[7] = 'M';
    data[8] = ' ';
    data[9] = ' ';
    data[10] = ' ';
    data[11] = ' ';
    data[12] = ' ';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';                         // ID string (16 chars total)
    data[21] = SP_TYPE_BYTE_FUJINET_CPM;    // Device type    - 0x02  harddisk
    data[22] = SP_SUBTYPE_BYTE_FUJINET_CPM; // Device Subtype - 0x0a
    data[23] = 0x00;                        // Firmware version 2 bytes
    data[24] = 0x01;                        //
    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

void iwmCPM::sio_status()
{
    // Nothing to do here
    return;
}

void iwmCPM::iwm_open(iwm_decoded_cmd_t cmd)
{
    // Debug_printf("\r\nOpen CP/M Unit # %02x\n", cmd.g7byte1);
    send_status_reply_packet();
}

void iwmCPM::iwm_close(iwm_decoded_cmd_t cmd)
{
    // Probably need to send close command here.
    // Debug_printf("\r\nClose CP/M Unit # %02x\n", cmd.g7byte1);
    send_status_reply_packet();
}

void iwmCPM::iwm_status(iwm_decoded_cmd_t cmd)
{
    unsigned short mw;
    // uint8_t source = cmd.dest;                                                // we are the destination and will become the source // packet_buffer[6];
    uint8_t status_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    Debug_printf("\r\nDevice %02x Status Code %02x\n", id(), status_code);
    // Debug_printf("\r\nStatus List is at %02x %02x\n", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

    switch (status_code)
    {
    case IWM_STATUS_STATUS: // 0x00
        send_status_reply_packet();
        return;
        break;
    // case IWM_STATUS_DCB:                  // 0x01
    // case IWM_STATUS_NEWLINE:              // 0x02
    case IWM_STATUS_DIB: // 0x03
        send_status_dib_reply_packet();
        return;
        break;
    case 'S': // Status
        mw = uxQueueMessagesWaiting(rxq);

        if (mw > 512)
            mw = 512;

        data_buffer[0] = mw & 0xFF;
        data_buffer[1] = mw >> 8;
        data_len = 2;
        Debug_printf("%u bytes waiting\n", mw);
        break;
    case 'B':
        data_buffer[0]=(cpmTaskHandle==NULL ? 0 : 1);
        data_len = 0;
        Debug_printf("CPM Task Running? %d",data_buffer[0]);
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
}

void iwmCPM::iwm_read(iwm_decoded_cmd_t cmd)
{
    uint16_t numbytes = get_numbytes(cmd); // cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    uint32_t addy = get_address(cmd);      // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    unsigned short mw = uxQueueMessagesWaiting(rxq);

    Debug_printf("\r\nDevice %02x READ %04x bytes from address %06x\n", id(), numbytes, addy);

    memset(data_buffer, 0, sizeof(data_buffer));

    if (numbytes > mw)
    {
        IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_IOERROR, data_buffer, 0);
        return;
    }

    memset(data_buffer, 0, sizeof(data_buffer));

    for (int i = 0; i < numbytes; i++)
    {
        char b;
        xQueueReceive(rxq, &b, portMAX_DELAY);
        data_buffer[i] = b;
        data_len++;
    }

    Debug_printf("\r\nsending block packet ...");
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));
}

void iwmCPM::iwm_write(iwm_decoded_cmd_t cmd)
{
    uint16_t num_bytes = get_numbytes(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);

    Debug_printf("\nWRITE %u bytes\n", num_bytes);

    // get write data packet, keep trying until no timeout
    //  to do - this blows up - check handshaking

    data_len = num_bytes;
    IWM.iwm_decode_data_packet(data_buffer, data_len); // write data packet now read in ISR
    // if (IWM.iwm_decode_data_packet(data_buffer, data_len))
    // {
    //     Debug_printf("\r\nTIMEOUT in read packet!");
    //     return;
    // }

    {
        // DO write
        for (int i = 0; i < num_bytes; i++)
            xQueueSend(txq, &data_buffer[i], portMAX_DELAY);
    }

    send_reply_packet(SP_ERR_NOERROR);
}

void iwmCPM::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    // uint8_t source = cmd.dest;                                                 // we are the destination and will become the source // data_buffer[6];
    uint8_t control_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // ctrl codes 00-FF
    Debug_printf("\r\nCPM Device %02x Control Code %02x", id(), control_code);
    // Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
    data_len = 512;
    IWM.iwm_decode_data_packet(data_buffer, data_len);
    // Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, data_buffer[12] & 0x7f);
    print_packet(data_buffer);

    if (data_len > 0)
        switch (control_code)
        {
        case 'B': // Boot
            if (!fnSystem.spifix())
            {
                err_result = SP_ERR_OFFLINE;
                Debug_printf("FujiApple SPI Fix Missing, not starting CP/M\n");
            }
            else
            {
                Debug_printf("!!! STARTING CP/M TASK!!!\n");
                if (cpmTaskHandle != NULL)
                {
                    break;
                }
                xTaskCreatePinnedToCore(cpmTask, "cpmtask", 32768, NULL, CPM_TASK_PRIORITY, &cpmTaskHandle, 1);
            }
            break;
        }
    else
        err_result = SP_ERR_IOERROR;

    send_reply_packet(err_result);
}

void iwmCPM::process(iwm_decoded_cmd_t cmd)
{
    switch (cmd.command)
    {
    case 0x00: // status
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case 0x04: // control
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case 0x08: // read
        fnLedManager.set(LED_BUS, true);
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        fnLedManager.set(LED_BUS, false);
        break;
    case 0x09: // write
        fnLedManager.set(LED_BUS, true);
        Debug_printf("\r\nHandling write command");
        iwm_write(cmd);
        fnLedManager.set(LED_BUS, false);
        break;
    default:
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
}

void iwmCPM::shutdown()
{
    // TODO: clean shutdown.
}

#endif /* BUILD_APPLE2 */