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
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR::NOERROR, data, 4);
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
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR::NOERROR, data.data(), data.size());

}

void iwmCPM::sio_status()
{
    // Nothing to do here
    return;
}

void iwmCPM::iwm_open(iwm_decoded_cmd_t cmd)
{
    spError_t err_result = SP_ERR::NOERROR;

    Debug_printf("\r\nCP/M: Open\n");
#ifdef ESP_PLATFORM // OS
    if (!fnSystem.hasbuffer())
    {
        err_result = SP_ERR::OFFLINE;
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
    send_reply_packet(SP_ERR::NOERROR);
}

void iwmCPM::iwm_status(iwm_decoded_cmd_t cmd)
{
    unsigned short mw;
    // uint8_t source = cmd.dest;                                                // we are the destination and will become the source // packet_buffer[6];
    Debug_printf("\r\n[CPM] Device %02x Status Code %02x\r\n", id(), cmd.control_status.fuji.command);
    // Debug_printf("\r\nStatus List is at %02x %02x\n", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

    // FIXME - enums have been mixed&matched, having to cast to int
    switch (static_cast<int>(cmd.control_status.code))
    {
    case SP_STAT_DEVICE: // 0x00
        send_status_reply_packet();
        return;
        break;
    // case SP_STAT_DCB:                  // 0x01
    // case SP_STAT_NEWLINE:              // 0x02
    case SP_STAT_DIB: // 0x03
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
        data_buffer[0]=(cpmTaskHandle==NULL ? 1 : 0);
#endif
        data_len = 1;
        Debug_printf("CPM Task Running? %d %s", data_buffer[0],(data_buffer[0]) ? "=No" : "=Yes");
        break;
    default:
        send_reply_packet(SP_ERR::BADCMD);
        return;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR, data_buffer, data_len);
}

void iwmCPM::iwm_read(iwm_decoded_cmd_t cmd)
{
#ifdef ESP_PLATFORM // OS
    unsigned short mw = uxQueueMessagesWaiting(rxq);
#else
    unsigned short mw;
#endif

    Debug_printf("\r\nDevice %02x READ %04x bytes from address %06lx\n", id(), cmd.char_rw.length, cmd.char_rw.address);

    memset(data_buffer, 0, sizeof(data_buffer));

    if (mw) // check if we really have some bytes waiting
    {
        if (mw < cmd.char_rw.length) //if there are less than requested, just send what we have
        {
            cmd.char_rw.length = mw;
        }

        data_len = 0;
        for (int i = 0; i < cmd.char_rw.length; i++)
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
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR, data_buffer, data_len);
    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));
}

void iwmCPM::iwm_write(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\nWRITE %u bytes\n", cmd.char_rw.length);

    // get write data packet, keep trying until no timeout
    //  to do - this blows up - check handshaking

    data_len = cmd.char_rw.length;
    SYSTEM_BUS.iwm_decode_data_packet(data_buffer, data_len); // write data packet now read in ISR
    // if (SYSTEM_BUS.iwm_decode_data_packet(data_buffer, data_len))
    // {
    //     Debug_printf("\r\nTIMEOUT in read packet!");
    //     return;
    // }

    {
        // DO write
#ifdef ESP_PLATFORM // OS
        for (int i = 0; i < cmd.char_rw.length; i++)
            xQueueSend(txq, &data_buffer[i], portMAX_DELAY);
#endif
    }

    send_reply_packet(SP_ERR::NOERROR);
}

void iwmCPM::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    spError_t err_result = SP_ERR::NOERROR;

    // uint8_t source = cmd.dest;                                                 // we are the destination and will become the source // data_buffer[6];
    Debug_printf("\r\nCPM Device %02x Control Code %02x", id(), cmd.control_status.fuji.command);
    // Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
    data_len = 512;
    SYSTEM_BUS.iwm_decode_data_packet(data_buffer, data_len);
    // Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, data_buffer[12] & 0x7f);
    print_packet(data_buffer);

    if (data_len > 0)
        switch (cmd.control_status.fuji.command)
        {
        case 'B': // Boot
#ifdef ESP_PLATFORM // OS
            if (!fnSystem.hasbuffer())
            {
                err_result = SP_ERR::OFFLINE;
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
                xTaskCreatePinnedToCore(cpmTask, "cpmtask", 8192, this, CPM_TASK_PRIORITY, &cpmTaskHandle, 0);
#endif
            }
            break;
        default:
            send_reply_packet(SP_ERR::BADCMD);
            return;
        }
    else
        err_result = SP_ERR::IOERROR;

    send_reply_packet(err_result);
}

void iwmCPM::shutdown()
{
    // TODO: clean shutdown.
}

#endif /* BUILD_APPLE */
