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

void iwmCPM::sio_status()
{
    // Nothing to do here
    return;
}

void iwmCPM::iwm_open(const iwm_decoded_cmd_t &cmd)
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

    SYSTEM_BUS.transaction_error(err_result);
}

void iwmCPM::iwm_close(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\nCP/M: Close\n");
    SYSTEM_BUS.transaction_error(SP_ERR::NOERROR);
}

void iwmCPM::iwm_status(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\r\n[CPM] Device %02x Status Code %02x\r\n", id(), cmd.command());

    switch (cmd.command())
    {
    case CPMCMD_STATUS:
        {
            u16le_t mw;
#ifdef ESP_PLATFORM // OS
            mw = uxQueueMessagesWaiting(rxq);
#endif

            mw = std::min<uint16_t>(512, mw);
            SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
            SYSTEM_BUS.transaction_send(&mw, sizeof(mw));
            Debug_printf("%u bytes waiting\n", mw);
        }
        break;
    case 'B':
        {
            uint8_t booted = false;
#ifdef ESP_PLATFORM // OS
            booted = cpmTaskHandle==NULL ? 1 : 0;
#endif
            SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
            SYSTEM_BUS.transaction_send(booted);
            Debug_printf("CPM Task Running? %d %s", booted, booted ? "=No" : "=Yes");
        }
        break;
    default:
        SYSTEM_BUS.transaction_error(SP_ERR::BADCMD);
        return;
    }

    Debug_printf("\r\nStatus code complete, sending response");
}

void iwmCPM::iwm_read(const iwm_decoded_cmd_t &cmd)
{
#ifdef ESP_PLATFORM // OS
    unsigned short mw = uxQueueMessagesWaiting(rxq);
#else
    unsigned short mw;
#endif

    Debug_printf("\r\nDevice %02x READ %04x bytes from address %06lx\n", id(), cmd.frame.char_rw.length, cmd.frame.char_rw.address);

    std::vector<uint8_t> buffer;
    if (mw) // check if we really have some bytes waiting
    {
        size_t numbytes = std::min<uint16_t>(mw, cmd.frame.char_rw.length);

        for (size_t i = 0; i < numbytes; i++)
        {
            uint8_t b;
#ifdef ESP_PLATFORM // OS
            xQueueReceive(rxq, &b, portMAX_DELAY);
#endif
            buffer[i] = b;
        }
    }

    Debug_printf("\r\nsending CPM read data packet ...");
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(buffer);
}

void iwmCPM::iwm_write(const iwm_decoded_cmd_t &cmd)
{
    Debug_printf("\nWRITE %u bytes\n", cmd.frame.char_rw.length);

    {
        auto buffer = cmd.data().value();
        // DO write
#ifdef ESP_PLATFORM // OS
        for (int i = 0; i < cmd.frame.char_rw.length; i++)
            xQueueSend(txq, &buffer[i], portMAX_DELAY);
#endif
    }

    SYSTEM_BUS.transaction_error(SP_ERR::NOERROR);
}

void iwmCPM::iwm_ctrl(const iwm_decoded_cmd_t &cmd)
{
    spError_t err_result = SP_ERR::NOERROR;

    Debug_printf("\r\nCPM Device %02x Control Code %02x", id(), cmd.command());

    if (cmd.data()->size() > 0)
      switch (cmd.command())
        {
        case CPMCMD_BOOT:
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
            SYSTEM_BUS.transaction_error(SP_ERR::BADCMD);
            return;
        }
    else
        err_result = SP_ERR::IOERROR;

    SYSTEM_BUS.transaction_error(err_result);
}

void iwmCPM::shutdown()
{
    // TODO: clean shutdown.
}

#endif /* BUILD_APPLE */
