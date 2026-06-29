#ifdef BUILD_COCO

#ifdef ESP_PLATFORM

#include "cpm.h"

#include "../../include/debug.h"

// The CP/M engine runs on its own task; read()/write() shuttle bytes to and
// from it through the cpmQueueDevice byte queues.  handle_cpm() runs one full
// CP/M session and returns when the program exits CP/M; the loop starts a fresh
// session afterwards, exactly like the old free-running cpmTask did.
static void cpmTask(void *arg)
{
    Debug_printf("cpmTask()\n");
    while (1)
    {
        vTaskDelay(100);
        theCPM.handle_cpm();
    }
}

void drivewireCPM::ready()
{
    SYSTEM_BUS.write(0x01);
}

void drivewireCPM::send_response()
{
    // Send body
    SYSTEM_BUS.write((uint8_t *)response.c_str(),response.length());

    // Clear the response
    response.clear();
    response.shrink_to_fit();
}

void drivewireCPM::boot()
{
    if (cpmTaskHandle != NULL)
    {
        vTaskDelete(cpmTaskHandle);
        cpmTaskHandle = NULL;
    }

    xTaskCreatePinnedToCore(cpmTask, "cpmtask", 32768, NULL, 20, &cpmTaskHandle, 1);
}

void drivewireCPM::read()
{
    uint8_t lenh = SYSTEM_BUS.read();
    uint8_t lenl = SYSTEM_BUS.read();
    uint16_t len = (lenh * 256) + lenl;
    uint16_t mw = host_available();

    if (!len)
        return;

    if (!mw)
        return;

    if (len > mw)
        len = mw;

    response.resize(len);
    size_t got = host_read((uint8_t *)&response[0], len);
    response.resize(got);
}

void drivewireCPM::write()
{
    uint8_t lenh = SYSTEM_BUS.read();
    uint8_t lenl = SYSTEM_BUS.read();
    uint16_t len = (lenh * 256) + lenl;

    if (!len)
        return;

    for (uint16_t i=0;i<len;i++)
    {
        uint8_t b = SYSTEM_BUS.read();
        host_write(&b, 1);
    }
}

void drivewireCPM::status()
{
    unsigned short mw = host_available();
    unsigned char status_response[2] = {0,0};

    status_response[0] = mw >> 8;
    status_response[1] = mw & 0xFF;

    response = std::string((const char *)status_response, 2);
}

void drivewireCPM::process()
{
    uint8_t cmd = SYSTEM_BUS.read();

    switch(cmd)
    {
        case 0x00:
            ready();
            break;
        case 0x01:
            send_response();
            break;
        case 'B':
            boot();
            break;
        case 'R':
            read();
            break;
        case 'W':
            write();
            break;
        case 'S':
            status();
            break;
    }
}

drivewireCPM theCPM;

#endif /* ESP_PLATFORM */

#endif /* BUILD_COCO */
