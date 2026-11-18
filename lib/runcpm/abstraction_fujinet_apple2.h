/**
 * Abstraction functions for #FujiNet
 */

#ifndef ABSTRACTION_FUJINET_APPLE2_H
#define ABSTRACTION_FUJINET_APPLE2_H

#include <queue>
#include <string.h>
#include <errno.h>
#include "compat_string.h"

#include "globals.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fnFsSD.h"
#include "fnTcpServer.h"
#include "fnTcpClient.h"

#include "iwm/iwmFuji.h"

#include "abstraction_fujinet_disk.h"

#define HostOS 0x07 // FUJINET

#ifdef BUILD_COCO
// This file says "apple2" right in the name so CoCo should follow apple2 convention, right?
#include "drivewire/drivewireFuji.h"
#define MAX_A2DISK_DEVICES MAX_DWDISK_DEVICES
#endif

#ifdef ESP_PLATFORM // OS
QueueHandle_t rxq;
QueueHandle_t txq;
#endif

fnTcpClient client;
fnTcpServer *server;
bool teeMode = false;
unsigned short portActive = 0;

//
// Hardware functions, new in 5.x
//
void _HardwareOut(const uint32 Port, const uint32 Value) {

}

uint32 _HardwareIn(const uint32 Port) {
        return 0;
}

/* Console abstraction functions */
/*===============================================================================*/

int _kbhit(void)
{
#ifdef ESP_PLATFORM // OS
        return uxQueueMessagesWaiting(txq);
#else
        return 0;
#endif
}

uint8_t _getch(void)
{
        uint8_t c;
#ifdef ESP_PLATFORM // OS
        xQueueReceive(txq,&c,portMAX_DELAY);
#endif
        return c;
}

uint8_t _getche(void)
{
        uint8_t c = _getch();
#ifdef ESP_PLATFORM // OS
        xQueueSend(rxq,&c,portMAX_DELAY);
#endif
        return c;
}

void _putch(uint8_t ch)
{
#ifdef ESP_PLATFORM // OS
        xQueueSend(rxq,&ch,portMAX_DELAY);
#endif
}

void _clrscr(void)
{
        _putch(0x1B);
        _putch('[');
        _putch('1');
        _putch(';');
        _putch('1');
        _putch('H');
        _putch(0x1B);
        _putch('[');
        _putch('2');
        _putch('J');
}

uint8_t bdos_networkConfig(uint16_t addr)
{
        // Response to CMD::FUJI_GET_ADAPTERCONFIG
        struct
        {
                char ssid[32];
                char hostname[64];
                unsigned char localIP[4];
                unsigned char gateway[4];
                unsigned char netmask[4];
                unsigned char dnsIP[4];
                unsigned char macAddress[6];
                unsigned char bssid[6];
                char fn_version[15];
        } cfg;

        memset(&cfg, 0, sizeof(cfg));

        strlcpy(cfg.fn_version, fnSystem.get_fujinet_version(true), sizeof(cfg.fn_version));

        if (!fnWiFi.connected())
        {
                strlcpy(cfg.ssid, "NOT CONNECTED", sizeof(cfg.ssid));
        }
        else
        {
                strlcpy(cfg.hostname, fnSystem.Net.get_hostname().c_str(), sizeof(cfg.hostname));
                strlcpy(cfg.ssid, fnWiFi.get_current_ssid().c_str(), sizeof(cfg.ssid));
                fnWiFi.get_current_bssid(cfg.bssid);
                fnSystem.Net.get_ip4_info(cfg.localIP, cfg.netmask, cfg.gateway);
                fnSystem.Net.get_ip4_dns_info(cfg.dnsIP);
        }

        fnWiFi.get_mac(cfg.macAddress);

        // Transfer to Z80 RAM.
        memset(&RAM[addr], 0, sizeof(cfg));
        memcpy(&RAM[addr], &cfg, sizeof(cfg));

        return 0;
}

uint8_t bdos_readHostSlots(uint16_t addr)
{
        char hostSlots[8][32];
        memset(hostSlots, 0, sizeof(hostSlots));

        for (int i = 0; i < 8; i++)
                strlcpy(hostSlots[i], SYSTEM_BUS.theFuji->get_host(i)->get_hostname(), 32);

        memset(&RAM[addr], 0, sizeof(hostSlots));
        memcpy(&RAM[addr], &hostSlots, sizeof(hostSlots));
        return 0;
}

uint8_t bdos_readDeviceSlots(uint16_t addr)
{
        struct disk_slot
        {
                uint8_t hostSlot;
                uint8_t mode;
                char filename[MAX_DISPLAY_FILENAME_LEN];
        };
        disk_slot diskSlots[MAX_A2DISK_DEVICES];

        // Load the data from our current device array
        for (int i = 0; i < MAX_A2DISK_DEVICES; i++)
        {
                diskSlots[i].mode = SYSTEM_BUS.theFuji->get_disk(i)->access_mode;
                diskSlots[i].hostSlot = SYSTEM_BUS.theFuji->get_disk(i)->host_slot;
                strlcpy(diskSlots[i].filename, SYSTEM_BUS.theFuji->get_disk(i)->filename, MAX_DISPLAY_FILENAME_LEN);
        }

        // Transfer to Z80 RAM.
        memset(&RAM[addr], 0, sizeof(diskSlots));
        memcpy(&RAM[addr], &diskSlots, sizeof(diskSlots));

        return 0;
}

uint8_t bios_tcpListen(uint16_t port)
{
        Debug_printf("Do we get here?\r\n");

        if (client.connected())
                client.stop();

        if (server != nullptr && port != portActive)
        {
                server->stop();
                delete server;
        }

        server = new fnTcpServer(port,1);
        int res = server->begin(port);
        if (res == 0)
        {
                Debug_printf("bios_tcpListen - failed to open port %u\nError (%d): %s\r\n", port, errno, strerror(errno));
                return true;
        }
        else
        {
                Debug_printf("bios_tcpListen - Now listening on port %u\r\n", port);
                return false;
        }
}

uint8_t bios_tcpAvailable(void)
{
        if (server == nullptr)
                return 0;

        return server->hasClient();
}

uint8_t bios_tcpTeeAccept(void)
{
        if (server == nullptr)
                return false;

        if (server->hasClient())
                client = server->accept();

        teeMode = true;

        return client.connected();
}

uint8_t bios_tcpDrop(void)
{
        if (server == nullptr)
                return false;

        client.stop();

        return true;
}

#endif /* ABSTRACTION_FUJINET_APPLE2_H */
