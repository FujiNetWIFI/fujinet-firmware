#ifdef BUILD_APPLE
#define CCP_INTERNAL

#include "cpm.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fujiDevice.h"
#include "fnFS.h"
#include "fnFsSD.h"
#include "fnConfig.h"
#include "compat_string.h"

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

#define CPM_TASK_PRIORITY 10

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
        _puts(CCPHEAD);
        _PatchCPM();
        _ccp();
    }
}

iwmCPM::iwmCPM()
{
#ifdef ESP_PLATFORM // OS
    rxq = xQueueCreate(2048, sizeof(char));
    txq = xQueueCreate(2048, sizeof(char));
#endif
}

void iwmCPM::send_status_reply_packet()
{
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 4);
}

void iwmCPM::send_status_dib_reply_packet()
{
        Debug_printf("\r\nCPM: Sending DIB reply\r\n");
        std::vector<uint8_t> data = create_dib_reply_packet(
                "CPM",                                                      // name
                STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE,             // status
                { 0, 0, 0 },                                                // block size
                { SP_TYPE_BYTE_FUJINET_CPM, SP_SUBTYPE_BYTE_FUJINET_CPM },  // type, subtype
                { 0x00, 0x01 }                                              // version.
        );
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data.data(), data.size());

}

void iwmCPM::sio_status()
{
    // Nothing to do here
    return;
}

void iwmCPM::iwm_open(iwm_decoded_cmd_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    Debug_printf("\r\nCP/M: Open\n");
#ifdef ESP_PLATFORM // OS
    if (!fnSystem.hasbuffer())
    {
        err_result = SP_ERR_OFFLINE;
    Debug_printf("FujiApple HASBUFFER Missing, not starting CP/M\n");
    }
    else
    {
        if (cpmTaskHandle == NULL)
        {
            Debug_printf("!!! STARTING CP/M TASK!!!\n");
            xTaskCreatePinnedToCore(cpmTask, "cpmtask", 4096, this, CPM_TASK_PRIORITY, &cpmTaskHandle, 0);
        }
    }
#endif

    send_reply_packet(err_result);
}

void iwmCPM::iwm_close(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\nCP/M: Close\n");
    send_reply_packet(SP_ERR_NOERROR);
}

void iwmCPM::iwm_status(iwm_decoded_cmd_t cmd)
{
    unsigned short mw;
    // uint8_t source = cmd.dest;                                                // we are the destination and will become the source // packet_buffer[6];
    uint8_t status_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    Debug_printf("\r\n[CPM] Device %02x Status Code %02x\r\n", id(), status_code);
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
#ifdef ESP_PLATFORM // OS
        mw = uxQueueMessagesWaiting(rxq);
#endif

        if (mw > 512)
            mw = 512;

        data_buffer[0] = mw & 0xFF;
        data_buffer[1] = mw >> 8;
        data_len = 2;
        Debug_printf("%u bytes waiting\n", mw);
        break;
    case 'B':
#ifdef ESP_PLATFORM // OS
        data_buffer[0]=(cpmTaskHandle==NULL ? 0 : 1);
#endif
        data_len = 0;
        Debug_printf("CPM Task Running? %d",data_buffer[0]);
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
}

void iwmCPM::iwm_read(iwm_decoded_cmd_t cmd)
{
    uint16_t numbytes = get_numbytes(cmd); // cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    uint32_t addy = get_address(cmd);      // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
#ifdef ESP_PLATFORM // OS
    unsigned short mw = uxQueueMessagesWaiting(rxq);
#else
    unsigned short mw;
#endif

    Debug_printf("\r\nDevice %02x READ %04x bytes from address %06lx\n", id(), numbytes, addy);

    memset(data_buffer, 0, sizeof(data_buffer));

    if (mw) // check if we really have some bytes waiting
    {
        if (mw < numbytes) //if there are less than requested, just send what we have
        {
            numbytes = mw;
        }

        data_len = 0;
        for (int i = 0; i < numbytes; i++)
        {
            char b;
#ifdef ESP_PLATFORM // OS
            xQueueReceive(rxq, &b, portMAX_DELAY);
#endif
            data_buffer[i] = b;
            data_len++;
        }
    }
    else // no bytes waiting, just reply back with no data
    {
        data_len = 0;
    }

    Debug_printf("\r\nsending CPM read data packet ...");
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
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
    SYSTEM_BUS.iwm_decode_data_packet(data_buffer, data_len); // write data packet now read in ISR
    // if (SYSTEM_BUS.iwm_decode_data_packet(data_buffer, data_len))
    // {
    //     Debug_printf("\r\nTIMEOUT in read packet!");
    //     return;
    // }

    {
        // DO write
#ifdef ESP_PLATFORM // OS
        for (int i = 0; i < num_bytes; i++)
            xQueueSend(txq, &data_buffer[i], portMAX_DELAY);
#endif
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
    SYSTEM_BUS.iwm_decode_data_packet(data_buffer, data_len);
    // Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, data_buffer[12] & 0x7f);
    print_packet(data_buffer);

    if (data_len > 0)
        switch (control_code)
        {
        case 'B': // Boot
#ifdef ESP_PLATFORM // OS
            if (!fnSystem.hasbuffer())
            {
                err_result = SP_ERR_OFFLINE;
                Debug_printf("FujiApple HASBUFFER Missing, not starting CP/M\n");
            }
            else
#endif
            {
                Debug_printf("!!! STARTING CP/M TASK!!!\n");
#ifdef ESP_PLATFORM // OS
                if (cpmTaskHandle != NULL)
                {
                        break;
                }
                xTaskCreatePinnedToCore(cpmTask, "cpmtask", 4096, this, CPM_TASK_PRIORITY, &cpmTaskHandle, 0);
#endif
            }
            break;
        }
    else
        err_result = SP_ERR_IOERROR;

    send_reply_packet(err_result);
}

void iwmCPM::process(iwm_decoded_cmd_t cmd)
{
    // Respond with device offline if cp/m is disabled
    if ( !Config.get_cpm_enabled() )
    {
        iwm_return_device_offline(cmd);
        return;
    }

    switch (cmd.command)
    {
    case SP_CMD_STATUS:
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case SP_CMD_CONTROL:
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case SP_CMD_OPEN:
        Debug_printf("\r\nhandling open command");
        iwm_open(cmd);
        break;
    case SP_CMD_CLOSE:
        Debug_printf("\r\nhandling close command");
        iwm_close(cmd);
        break;
    case SP_CMD_READ:
        fnLedManager.set(LED_BUS, true);
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        fnLedManager.set(LED_BUS, false);
        break;
    case SP_CMD_WRITE:
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

#endif /* BUILD_APPLE */
