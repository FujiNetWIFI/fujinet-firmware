#ifdef BUILD_APPLE

/*
 * iwm/cpm.cpp — Apple II SmartPort CP/M transport, now a thin console back-end.
 *
 * The RunCPM engine + state live once in lib/runcpm/runcpm_core.cpp.  This file
 * no longer includes the RunCPM header chain (which, since RunCPM 6.9, defines
 * the `Command` struct and the `CPM` macro that collide with the Apple-II bus
 * headers, e.g. devrelay/types/Command.h and improv.h); it only supplies a
 * queue-based console back-end (runcpm_console_ops) over the SmartPort rx/tx
 * queues and asks the shared core to run a session.
 */

#include "cpm.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fujiDevice.h"
#include "fnFS.h"
#include "fnFsSD.h"
#include "fnConfig.h"
#include "compat_string.h"

#include "../hardware/led.h"

#include "../runcpm/runcpm_session.h"

#define CPM_TASK_PRIORITY 10

/*
 * SmartPort console queues (this TU owns them).
 *   rxq : CP/M stdout -> host read  (putch pushes; iwm_read pops)
 *   txq : host write  -> CP/M stdin (iwm_write pushes; getch pops)
 * They are FreeRTOS queues, so they only exist on the ESP target; on
 * FujiNet-PC the Apple-II CP/M console is inert (as it was before).
 */
#ifdef ESP_PLATFORM // OS
static QueueHandle_t rxq = nullptr;
static QueueHandle_t txq = nullptr;
#endif

/* ----- console back-end (runcpm_console_ops) ----- */

static int cpm_kbhit(void)
{
#ifdef ESP_PLATFORM // OS
    return txq ? (int)uxQueueMessagesWaiting(txq) : 0;
#else
    return 0;
#endif
}

static int cpm_getch(void)
{
    uint8_t c = 0;
#ifdef ESP_PLATFORM // OS
    if (txq)
        xQueueReceive(txq, &c, portMAX_DELAY);
#endif
    return c;
}

static int cpm_getche(void)
{
    uint8_t c = (uint8_t)cpm_getch();
#ifdef ESP_PLATFORM // OS
    if (rxq)
        xQueueSend(rxq, &c, portMAX_DELAY);
#endif
    return c;
}

static void cpm_putch(uint8_t ch)
{
#ifdef ESP_PLATFORM // OS
    if (rxq)
        xQueueSend(rxq, &ch, portMAX_DELAY);
#else
    (void)ch;
#endif
}

static void cpm_clrscr(void)
{
    /* VT100 cursor-home + clear-screen */
    static const uint8_t seq[] = {0x1B, '[', '1', ';', '1', 'H',
                                  0x1B, '[', '2', 'J'};
    for (size_t i = 0; i < sizeof(seq); i++)
        cpm_putch(seq[i]);
}

static const runcpm_console_ops cpm_console_ops = {
    cpm_getch,
    cpm_getche,
    cpm_kbhit,
    cpm_putch,
    cpm_clrscr,
};

#ifdef ESP_PLATFORM // OS
static void cpmTask(void *arg)
{
    (void)arg;
    Debug_printf("cpmTask()\n");
    // The shared core owns the CCP + warm-boot loop; keep re-running it so the
    // SmartPort console behaves like the original always-on task.
    while (1)
        runcpm_session_run(&cpm_console_ops);
}
#endif

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
        send_reply_packet(SP_ERR_BADCMD);
        return;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
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
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
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

    send_reply_packet(SP_ERR_NOERROR);
}

void iwmCPM::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

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
                xTaskCreatePinnedToCore(cpmTask, "cpmtask", 8192, this, CPM_TASK_PRIORITY, &cpmTaskHandle, 0);
#endif
            }
            break;
        default:
            send_reply_packet(SP_ERR_BADCMD);
            return;
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

    switch (cmd.sp_command)
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
