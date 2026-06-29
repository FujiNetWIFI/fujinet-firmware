#ifdef BUILD_APPLE

#include "cpm.h"

#include <cstring>

#include "../../include/debug.h"
#include "fnSystem.h"
#include "compat_string.h"

#define CPM_TASK_PRIORITY 10

#ifdef ESP_PLATFORM // OS
// The CP/M engine runs on its own task; iwm_read/iwm_write shuttle bytes to and
// from it through the cpmQueueDevice byte queues.  handle_cpm() runs one full
// CP/M session (it returns when the program exits CP/M); the loop starts a
// fresh session afterwards, exactly like the old free-running cpmTask did.
static void cpmTask(void *arg)
{
    Debug_printf("cpmTask()\n");
    iwmCPM *dev = static_cast<iwmCPM *>(arg);
    while (1)
    {
        dev->handle_cpm();
    }
}
#endif

iwm_device_status_block_t iwmCPM::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = STATCODE_WRITE_ALLOWED | STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  status.block_size = 0;
  return status;
}

iwm_device_info_block_t iwmCPM::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  strcpy(dib.name, "CPM");
  dib.name_len = strlen(dib.name);
  dib.type = SP_TYPE_BYTE_FUJINET_CPM;
  dib.subtype = SP_SUBTYPE_BYTE_FUJINET_CPM;
  dib.version = 0x0100;

  return dib;
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

    switch (cmd.control_status.fuji.command)
    {
    case 'S': // Status
        mw = host_available();

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
#else
        data_buffer[0]=1;
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
    unsigned short mw = host_available();

    Debug_printf("\r\nDevice %02x READ %04x bytes from address %06lx\n", id(), cmd.char_rw.length, cmd.char_rw.address);

    memset(data_buffer, 0, sizeof(data_buffer));

    if (mw) // check if we really have some bytes waiting
    {
        if (mw < cmd.char_rw.length) //if there are less than requested, just send what we have
        {
            cmd.char_rw.length = mw;
        }

        data_len = host_read(data_buffer, cmd.char_rw.length);
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

    host_write(data_buffer, cmd.char_rw.length);

    send_reply_packet(SP_ERR::NOERROR);
}

void iwmCPM::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    spError_t err_result = SP_ERR::NOERROR;

    // uint8_t source = cmd.dest;                                                 // we are the destination and will become the source // data_buffer[6];
    Debug_printf("\r\nCPM Device %02x Control Code %02x", id(), cmd.control_status.fuji.command);
    // Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
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
