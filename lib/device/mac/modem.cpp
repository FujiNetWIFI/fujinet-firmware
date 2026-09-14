#ifdef BUILD_MAC

#include <string.h>

#include "modem.h"
#include "fnWiFi.h"
#include "fnFsSPIFFS.h"
#include "fnSystem.h"
#include "utils.h"
#include "fnConfig.h"
#include "led.h"

macModem::macModem(FileSystem *_fs, bool snifferEnable)
{
    activeFS = _fs;
    modemSniffer = new ModemSniffer(activeFS, snifferEnable);
    // set_term_type("dumb");
    // telnet = telnet_init(telopts, _telnet_event_handler, 0, this);
    // mrxq = xQueueCreate(16384, sizeof(char));
    // mtxq = xQueueCreate(16384, sizeof(char));
    // xTaskCreatePinnedToCore(_modem_task, "modemTask", 4096, this, MODEM_TASK_PRIORITY, &modemTask, MODEM_TASK_CPU);
}

macModem::~macModem()
{
    if (modemSniffer != nullptr)
    {
        delete modemSniffer;
        modemSniffer = nullptr;
    }

    // if (telnet != nullptr)
    // {
    //     telnet_free(telnet);
    // }

    // vTaskDelete(modemTask);
    // vQueueDelete(mrxq);
    // vQueueDelete(mtxq);
}



#endif
