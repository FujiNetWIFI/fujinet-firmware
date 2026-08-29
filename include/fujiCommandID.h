#ifndef FUJI_COMMANDS_H
#define FUJI_COMMANDS_H

#include <stdint.h>

#ifdef PRINTER_WRITE
#undef PRINTER_WRITE
#endif

/*
 * Fuji Device Command Definitions
 */

typedef enum class CMD : uint8_t {
    FUJI_RESET                      = 0xFF,
    FUJI_GET_SSID                   = 0xFE,
    FUJI_SCAN_NETWORKS              = 0xFD,
    FUJI_GET_SCAN_RESULT            = 0xFC,
    FUJI_SET_SSID                   = 0xFB,
    FUJI_GET_WIFISTATUS             = 0xFA,
    FUJI_MOUNT_HOST                 = 0xF9,
    FUJI_MOUNT_IMAGE                = 0xF8,
    FUJI_OPEN_DIRECTORY             = 0xF7,
    FUJI_READ_DIR_ENTRY             = 0xF6,
    FUJI_CLOSE_DIRECTORY            = 0xF5,
    FUJI_READ_HOST_SLOTS            = 0xF4,
    FUJI_WRITE_HOST_SLOTS           = 0xF3,
    FUJI_READ_DEVICE_SLOTS          = 0xF2,
    FUJI_WRITE_DEVICE_SLOTS         = 0xF1,
    FUJI_ENABLE_UDPSTREAM           = 0xF0,
    FUJI_SET_BAUDRATE               = 0xEB,
    FUJI_GET_WIFI_ENABLED           = 0xEA,
    FUJI_UNMOUNT_IMAGE              = 0xE9,
    FUJI_GET_ADAPTERCONFIG          = 0xE8,
    FUJI_NEW_DISK                   = 0xE7,
    FUJI_UNMOUNT_HOST               = 0xE6,
    FUJI_GET_DIRECTORY_POSITION     = 0xE5,
    FUJI_SET_DIRECTORY_POSITION     = 0xE4,
    FUJI_SET_HSIO_INDEX             = 0xE3,
    FUJI_SET_DEVICE_FULLPATH        = 0xE2,
    FUJI_SET_HOST_PREFIX            = 0xE1,
    FUJI_GET_HOST_PREFIX            = 0xE0,
    FUJI_SET_SIO_EXTERNAL_CLOCK     = 0xDF,
    FUJI_WRITE_APPKEY               = 0xDE,
    FUJI_READ_APPKEY                = 0xDD,
    FUJI_OPEN_APPKEY                = 0xDC,
    FUJI_CLOSE_APPKEY               = 0xDB,
    FUJI_GET_DEVICE_FULLPATH        = 0xDA,
    FUJI_CONFIG_BOOT                = 0xD9,
    FUJI_COPY_FILE                  = 0xD8,
    FUJI_MOUNT_ALL                  = 0xD7,
    FUJI_SET_BOOT_MODE              = 0xD6,
    FUJI_ENABLE_DEVICE              = 0xD5,
    FUJI_DISABLE_DEVICE             = 0xD4,
    FUJI_RANDOM_NUMBER              = 0xD3,
    FUJI_GET_TIME                   = 0xD2,
    FUJI_DEVICE_ENABLE_STATUS       = 0xD1,
    FUJI_BASE64_ENCODE_INPUT        = 0xD0,
    FUJI_BASE64_ENCODE_COMPUTE      = 0xCF,
    FUJI_BASE64_ENCODE_LENGTH       = 0xCE,
    FUJI_BASE64_ENCODE_OUTPUT       = 0xCD,
    FUJI_BASE64_DECODE_INPUT        = 0xCC,
    FUJI_BASE64_DECODE_COMPUTE      = 0xCB,
    FUJI_BASE64_DECODE_LENGTH       = 0xCA,
    FUJI_BASE64_DECODE_OUTPUT       = 0xC9,
    FUJI_HASH_INPUT                 = 0xC8,
    FUJI_HASH_COMPUTE               = 0xC7,
    FUJI_HASH_LENGTH                = 0xC6,
    FUJI_HASH_OUTPUT                = 0xC5,
    FUJI_GET_ADAPTERCONFIG_EXTENDED = 0xC4,
    FUJI_HASH_COMPUTE_NO_CLEAR      = 0xC3,
    FUJI_HASH_CLEAR                 = 0xC2,
    FUJI_GET_HEAP                   = 0xC1,
    FUJI_QRCODE_OUTPUT              = 0xBF,
    FUJI_QRCODE_LENGTH              = 0xBE,
    FUJI_QRCODE_ENCODE              = 0xBD,
    FUJI_QRCODE_INPUT               = 0xBC,
    FUJI_GENERATE_GUID              = 0xBB,
    FUJI_GET_DEVICE10_FULLPATH      = 0xA9,
    FUJI_GET_DEVICE9_FULLPATH       = 0xA8,
    FUJI_GET_DEVICE8_FULLPATH       = 0xA7,
    FUJI_GET_DEVICE7_FULLPATH       = 0xA6,
    FUJI_GET_DEVICE6_FULLPATH       = 0xA5,
    FUJI_GET_DEVICE5_FULLPATH       = 0xA4,
    FUJI_GET_DEVICE4_FULLPATH       = 0xA3,
    FUJI_GET_DEVICE3_FULLPATH       = 0xA2,
    FUJI_GET_DEVICE2_FULLPATH       = 0xA1,
    FUJI_GET_DEVICE1_FULLPATH       = 0xA0,
    FUJI_UPDATE_FIRMWARE            = 0x90,
    FUJI_STATUS                     = 0x53, // S
    FUJI_HSIO_INDEX                 = 0x3F, // ?
    FUJI_NAK                        = 0x15, // ASCII NAK
    FUJI_ACK                        = 0x06, // ASCII ACK
    FUJI_SEND_ERROR                 = 0x02,
    FUJI_SEND_RESPONSE              = 0x01,
    FUJI_DEVICE_READY               = 0x00,

    DISK_HSIO_WRITE                 = 0xD7,
    DISK_HSIO_STATUS                = 0xD3,
    DISK_HSIO_READ                  = 0xD2,
    DISK_HSIO_PUT                   = 0xD0,
    DISK_HSIO_FORMAT_MEDIUM         = 0xA2,
    DISK_HSIO_FORMAT                = 0xA1,
    DISK_WRITE                      = 0x57, // W
    DISK_STATUS                     = 0x53, // S
    DISK_READ                       = 0x52, // R
    DISK_PUT                        = 0x50, // P
    DISK_PERCOM_WRITE               = 0x4F, // O
    DISK_PERCOM_READ                = 0x4E, // N
    DISK_HSIO_INDEX                 = 0x3F, // ?
    DISK_FORMAT_MEDIUM              = 0x22, // "
    DISK_FORMAT                     = 0x21, // !

    NET_GET_DSTATS_VALUE            = 0xFF,
    NET_PASSWORD                    = 0xFE,
    NET_USERNAME                    = 0xFD,
    NET_CHANNEL_MODE                = 0xFC,
    NET_SET_PARAMETERS              = 0xFB,
    NET_SET_CHANNEL                 = 0xFA,
    NET_SET_HSIO_INDEX              = 0xE3,
    NET_QUERY_ALT                   = 0x81,
    NET_PARSE_ALT                   = 0x80,
    NET_GET_REMOTE                  = 0x72, // r
    NET_CLOSE_CLIENT                = 0x63, // c
    NET_SET_INT_RATE                = 0x5A, // Z
    NET_WRITE                       = 0x57, // W
    NET_TRANSLATION                 = 0x54, // T
    NET_STATUS                      = 0x53, // S
    NET_READ                        = 0x52, // R
    NET_QUERY                       = 0x51, // Q
    NET_PARSE                       = 0x50, // P
    NET_OPEN                        = 0x4F, // O
    NET_SET_CHANNEL_MODE            = 0x4D, // M
    NET_SET_EOL                     = 0x4C, // L
    NET_GET_ERROR                   = 0x45, // E
    NET_SET_DESTINATION             = 0x44, // D
    NET_CLOSE                       = 0x43, // C
    NET_CONTROL                     = 0x41, // A
    NET_HSIO_INDEX                  = 0x3F, // ?
    NET_GETCWD                      = 0x30, // 0
    NET_CHDIR                       = 0x2C, // ,
    NET_RMDIR                       = 0x2B, // +
    NET_MKDIR                       = 0x2A, // *
    NET_TELL                        = 0x26, // &
    NET_SEEK                        = 0x25, // %
    NET_UNLOCK                      = 0x24, // $
    NET_LOCK                        = 0x23, // #
    NET_DELETE                      = 0x21, // !
    NET_RENAME                      = 0x20,

    MODEM_STREAM                    = 0x58, // X
    MODEM_WRITE                     = 0x57, // W
    MODEM_STATUS                    = 0x53, // S
    MODEM_READ                      = 0x52, // R
    MODEM_AUTOANSWER                = 0x4F, // O
    MODEM_BAUDRATELOCK              = 0x4E, // N
    MODEM_UNLISTEN                  = 0x4D, // M
    MODEM_LISTEN                    = 0x4C, // L
    MODEM_SET_DUMP                  = 0x44, // D
    MODEM_CONFIGURE                 = 0x42, // B
    MODEM_CONTROL                   = 0x41, // A
    MODEM_TYPE3_POLL                = 0x40, // @
    MODEM_TYPE1_POLL                = 0x3F, // ?
    MODEM_LOAD_HANDLER              = 0x26, // &
    MODEM_LOAD_RELOCATOR            = 0x21, // !

    APETIME_GETTZTIME               = 0x9A,
    APETIME_SETTZ                   = 0x99,
    APETIME_GETTIME                 = 0x93,
    APETIME_GET_ISO_UTC_ALT         = 0x7A, // z
    APETIME_SETTZ_ALT               = 0x74, // t
    APETIME_GET_SOS_ALT             = 0x73, // s
    APETIME_GET_PRODOS_ALT          = 0x70, // p
    APETIME_GET_ISO_LOCAL_ALT       = 0x69, // i
    APETIME_GET_ATARI_ALT           = 0x61, // a
    APETIME_GET_ISO_UTC             = 0x5A, // Z
    APETIME_SETTZ_ALT2              = 0x54, // T
    APETIME_GET_SOS                 = 0x53, // S
    APETIME_GET_PRODOS              = 0x50, // P
    APETIME_GET_SIMPLE_HUNDREDTHS   = 0x4D, // M - simple binary + 1-byte hundredths (0-99)
    APETIME_GETTZ_LEN               = 0x4C, // L
    APETIME_GET_ISO_LOCAL           = 0x49, // I
    APETIME_GET_GENERAL             = 0x47, // G
    APETIME_GET_ATARI               = 0x41, // A

    CPM_WRITE                       = 0x57, // W
    CPM_STATUS                      = 0x53, // S
    CPM_READ                        = 0x52, // R
    CPM_INIT                        = 0x47, // G
    CPM_BOOT                        = 0x42, // B

    PRINTER_WRITE                   = 0x57, // W
    PRINTER_STATUS                  = 0x53, // S
    PRINTER_PUT                     = 0x50, // P

    PCLINK_STATUS                   = 0x53, // S
    PCLINK_EXEC                     = 0x52, // R
    PCLINK_PARBLK                   = 0x50, // P
    PCLINK_HSI                      = 0x3F, // ?
} fujiCommandID_t;

#endif /* FUJI_COMMANDS_H */
