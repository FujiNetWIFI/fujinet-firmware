#ifndef DEVICE_FUJI_H
#define DEVICE_FUJI_H

#include <stdint.h>

#define MAX_HOSTS 8
#define MAX_DISK_DEVICES 8
#define MAX_NETWORK_DEVICES 4

#define MAX_SSID_LEN 32
#define MAX_WIFI_PASS_LEN 64

#define READ_DEVICE_SLOTS_DISKS1 0x00
#define READ_DEVICE_SLOTS_TAPE 0x10

#define MAX_APPKEY_LEN 64

enum appkey_mode : int8_t
{
    APPKEYMODE_INVALID = -1,
    APPKEYMODE_READ = 0,
    APPKEYMODE_WRITE,
    APPKEYMODE_READ_256
};

struct appkey
{
    uint16_t creator = 0;
    uint8_t app = 0;
    uint8_t key = 0;
    appkey_mode mode = APPKEYMODE_INVALID;
    uint8_t reserved = 0;
} __attribute__((packed));

#define ADAPTER_CONFIG_FIELDS \
    char ssid[33]; \
    char hostname[64]; \
    unsigned char localIP[4]; \
    unsigned char gateway[4]; \
    unsigned char netmask[4]; \
    unsigned char dnsIP[4]; \
    unsigned char macAddress[6]; \
    unsigned char bssid[6]; \
    char fn_version[15]

typedef struct
{
    ADAPTER_CONFIG_FIELDS;
} __attribute__((packed)) AdapterConfig;

typedef struct
{
    ADAPTER_CONFIG_FIELDS;
    char sLocalIP[16];
    char sGateway[16];
    char sNetmask[16];
    char sDnsIP[16];
    char sMacAddress[18];
    char sBssid[18];
} __attribute__((packed)) AdapterConfigExtended;

#ifdef BUILD_ATARI
# include "sio/sioFuji.h"
#endif

#ifdef BUILD_RS232
# include "rs232/rs232Fuji.h"
#endif

#ifdef BUILD_IEC
# include "iec/iecFuji.h"
#endif

#ifdef BUILD_ADAM
# include "adamnet/adamFuji.h"
#endif

#ifdef BUILD_LYNX
# include "comlynx/lynxFuji.h"
#endif

#ifdef BUILD_S100
# include "s100spi/s100spiFuji.h"
#endif

#ifdef BUILD_APPLE
# include "iwm/iwmFuji.h"
#endif

#ifdef BUILD_MAC
# include "mac/macFuji.h"
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/cx16Fuji.h"
#endif

#ifdef BUILD_RC2014
# include "rc2014/rc2014Fuji.h"
#endif

#ifdef BUILD_H89
# include "h89/H89Fuji.h"
#endif

#ifdef BUILD_COCO
# include "drivewire/drivewireFuji.h"
#endif

#endif // DEVICE_FUJI_H
