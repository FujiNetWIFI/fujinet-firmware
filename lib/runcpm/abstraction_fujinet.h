/**
 * Abstraction functions for #FujiNet
 */

#ifndef ABSTRACTION_FUJINET_H
#define ABSTRACTION_FUJINET_H

#include <string.h>
#include <errno.h>
#include "compat_string.h"

#include "globals.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"
#include "fnFsSD.h"
#ifdef ESP_PLATFORM
#include "IOChannel.h"
#endif
#include "fnTcpServer.h"
#include "fnTcpClient.h"

#include "fujiDevice.h"

#include "abstraction_fujinet_disk.h"

// Why is CP/M writing directly to the SYSTEM_BUS?
#define FN_CPM_LINK SYSTEM_BUS

#define HostOS 0x07 // FUJINET

fnTcpClient client;
fnTcpServer *server;
bool teeMode = false;
unsigned short portActive = 0;

//
// Hardware functions, new in 5.x
//
void _HardwareOut(const uint32 Port, const uint32 Value)
{

}

uint32 _HardwareIn(const uint32 Port)
{
    return 0;
}

/* Console abstraction functions */
/*===============================================================================*/

// A bus's CP/M device defines BYPASS_BUS before including this to use the bus as the console
#ifdef BYPASS_BUS
#define _kbhit() SYSTEM_BUS.available()
#define _cread() SYSTEM_BUS.read()
#define _cwrite(ch) SYSTEM_BUS.write(ch)
#else
#define _kbhit() 0
#define _cread() 0
#define _cwrite(ch)
#endif // BYPASS_BUS

uint8_t _getch(void)
{
    if (teeMode == true)
    {
        while (_kbhit() > 0)
        {
            if (client.available())
            {
                uint8_t ch;
                client.read(&ch, 1);
                return ch & 0x7F;
            }
        }
        return _cread() & 0x7F;
    }
    else
    {
        while (_kbhit() <= 0)
        {
        }
        return _cread() & 0x7f;
    }
}

uint8_t _getche(void)
{
    uint8_t ch = _getch() & 0x7f;
    _cwrite(ch);
    if (teeMode == true)
        client.write(ch);
    return ch;
}

void _putch(uint8_t ch)
{
    _cwrite(ch & 0x7f);
    if (teeMode == true)
        client.write(ch);
}

void _clrscr(void)
{
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
    disk_slot diskSlots[MAX_DISK_DEVICES];

    // Load the data from our current device array
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
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

#endif /* ABSTRACTION_FUJINET_H */
