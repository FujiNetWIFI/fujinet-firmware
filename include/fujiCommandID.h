#ifndef FUJI_COMMANDS_H
#define FUJI_COMMANDS_H

/*
 * Fuji Device Command Definitions
 */

enum fujiCommandID_t {
  FUJICMD_RESET                      = 0xFF,
  FUJICMD_GET_SSID                   = 0xFE,
  FUJICMD_SCAN_NETWORKS              = 0xFD,
  FUJICMD_GET_SCAN_RESULT            = 0xFC,
  FUJICMD_SET_SSID                   = 0xFB,
  FUJICMD_GET_WIFISTATUS             = 0xFA,
  FUJICMD_MOUNT_HOST                 = 0xF9,
  FUJICMD_MOUNT_IMAGE                = 0xF8,
  FUJICMD_OPEN_DIRECTORY             = 0xF7,
  FUJICMD_READ_DIR_ENTRY             = 0xF6,
  FUJICMD_CLOSE_DIRECTORY            = 0xF5,
  FUJICMD_READ_HOST_SLOTS            = 0xF4,
  FUJICMD_WRITE_HOST_SLOTS           = 0xF3,
  FUJICMD_READ_DEVICE_SLOTS          = 0xF2,
  FUJICMD_WRITE_DEVICE_SLOTS         = 0xF1,
  FUJICMD_ENABLE_UDPSTREAM           = 0xF0,
  FUJICMD_SET_BAUDRATE               = 0xEB,
  FUJICMD_GET_WIFI_ENABLED           = 0xEA,
  FUJICMD_UNMOUNT_IMAGE              = 0xE9,
  FUJICMD_GET_ADAPTERCONFIG          = 0xE8,
  FUJICMD_NEW_DISK                   = 0xE7,
  FUJICMD_UNMOUNT_HOST               = 0xE6,
  FUJICMD_GET_DIRECTORY_POSITION     = 0xE5,
  FUJICMD_SET_DIRECTORY_POSITION     = 0xE4,
  FUJICMD_SET_HSIO_INDEX             = 0xE3,
  FUJICMD_SET_DEVICE_FULLPATH        = 0xE2,
  FUJICMD_SET_HOST_PREFIX            = 0xE1,
  FUJICMD_GET_HOST_PREFIX            = 0xE0,
  FUJICMD_SET_SIO_EXTERNAL_CLOCK     = 0xDF,
  FUJICMD_WRITE_APPKEY               = 0xDE,
  FUJICMD_READ_APPKEY                = 0xDD,
  FUJICMD_OPEN_APPKEY                = 0xDC,
  FUJICMD_CLOSE_APPKEY               = 0xDB,
  FUJICMD_GET_DEVICE_FULLPATH        = 0xDA,
  FUJICMD_CONFIG_BOOT                = 0xD9,
  FUJICMD_COPY_FILE                  = 0xD8,
  FUJICMD_MOUNT_ALL                  = 0xD7,
  FUJICMD_SET_BOOT_MODE              = 0xD6,
  FUJICMD_ENABLE_DEVICE              = 0xD5,
  FUJICMD_DISABLE_DEVICE             = 0xD4,
  FUJICMD_RANDOM_NUMBER              = 0xD3,
  FUJICMD_GET_TIME                   = 0xD2,
  FUJICMD_DEVICE_ENABLE_STATUS       = 0xD1,
  FUJICMD_BASE64_ENCODE_INPUT        = 0xD0,
  FUJICMD_BASE64_ENCODE_COMPUTE      = 0xCF,
  FUJICMD_BASE64_ENCODE_LENGTH       = 0xCE,
  FUJICMD_BASE64_ENCODE_OUTPUT       = 0xCD,
  FUJICMD_BASE64_DECODE_INPUT        = 0xCC,
  FUJICMD_BASE64_DECODE_COMPUTE      = 0xCB,
  FUJICMD_BASE64_DECODE_LENGTH       = 0xCA,
  FUJICMD_BASE64_DECODE_OUTPUT       = 0xC9,
  FUJICMD_HASH_INPUT                 = 0xC8,
  FUJICMD_HASH_COMPUTE               = 0xC7,
  FUJICMD_HASH_LENGTH                = 0xC6,
  FUJICMD_HASH_OUTPUT                = 0xC5,
  FUJICMD_GET_ADAPTERCONFIG_EXTENDED = 0xC4,
  FUJICMD_HASH_COMPUTE_NO_CLEAR      = 0xC3,
  FUJICMD_HASH_CLEAR                 = 0xC2,
  FUJICMD_GET_HEAP                   = 0xC1,
  FUJICMD_QRCODE_OUTPUT              = 0xBF,
  FUJICMD_QRCODE_LENGTH              = 0xBE,
  FUJICMD_QRCODE_ENCODE              = 0xBD,
  FUJICMD_QRCODE_INPUT               = 0xBC,
  FUJICMD_GET_DEVICE8_FULLPATH       = 0xA7,
  FUJICMD_GET_DEVICE7_FULLPATH       = 0xA6,
  FUJICMD_GET_DEVICE6_FULLPATH       = 0xA5,
  FUJICMD_GET_DEVICE5_FULLPATH       = 0xA4,
  FUJICMD_GET_DEVICE4_FULLPATH       = 0xA3,
  FUJICMD_GET_DEVICE3_FULLPATH       = 0xA2,
  FUJICMD_GET_DEVICE2_FULLPATH       = 0xA1,
  FUJICMD_GET_DEVICE1_FULLPATH       = 0xA0,
  FUJICMD_GETTZTIME                  = 0x9A,
  FUJICMD_SETTZ                      = 0x99,
  FUJICMD_GETTIME                    = 0x93,
  FUJICMD_JSON_QUERY                 = 0x81,
  FUJICMD_JSON_PARSE                 = 0x80,
  FUJICMD_GET_REMOTE                 = 0x72, // r
  FUJICMD_CLOSE_CLIENT               = 0x63, // c
  FUJICMD_TIMER                      = 0x5A, // Z
  FUJICMD_STREAM                     = 0x58, // X
  FUJICMD_WRITE                      = 0x57, // W
  FUJICMD_TRANSLATION                = 0x54, // T
  FUJICMD_STATUS                     = 0x53, // S
  FUJICMD_READ                       = 0x52, // R
  FUJICMD_QUERY                      = 0x51, // Q
  FUJICMD_PUT                        = 0x50, // P
  FUJICMD_OPEN                       = 0x4F, // O
  FUJICMD_BAUDRATELOCK               = 0x4E, // N
  FUJICMD_UNLISTEN                   = 0x4D, // M
  FUJICMD_LISTEN                     = 0x4C, // L
  FUJICMD_CPM_INIT                   = 0x47, // G
  FUJICMD_GET_ERROR                  = 0x45, // E
  FUJICMD_SET_DESTINATION            = 0x44, // D
  FUJICMD_CLOSE                      = 0x43, // C
  FUJICMD_CONFIGURE                  = 0x42, // B
  FUJICMD_CONTROL                    = 0x41, // A
  FUJICMD_TYPE3_POLL                 = 0x40, // @
  FUJICMD_TYPE1_POLL                 = 0x3F, // ?
  FUJICMD_GETCWD                     = 0x30, // 0
  FUJICMD_CHDIR                      = 0x2C, // ,
  FUJICMD_RMDIR                      = 0x2B, // +
  FUJICMD_MKDIR                      = 0x2A, // *
  FUJICMD_TELL                       = 0x26, // &
  FUJICMD_SEEK                       = 0x25, // %
  FUJICMD_UNLOCK                     = 0x24, // $
  FUJICMD_LOCK                       = 0x23, // #
  FUJICMD_FORMAT_MEDIUM              = 0x22, // "
  FUJICMD_DELETE                     = 0x21, // !
  FUJICMD_RENAME                     = 0x20,
  FUJICMD_NAK                        = 0x15, // ASCII NAK
  FUJICMD_ACK                        = 0x06, // ASCII ACK
  FUJICMD_SEND_ERROR                 = 0x02,
  FUJICMD_SEND_RESPONSE              = 0x01,
  FUJICMD_DEVICE_READY               = 0x00,
};

#define FUJICMD_SPECIAL_QUERY FUJICMD_RESET
#define FUJICMD_PASSWORD FUJICMD_GET_SSID
#define FUJICMD_USERNAME FUJICMD_SCAN_NETWORKS
#define FUJICMD_JSON FUJICMD_GET_SCAN_RESULT
#define FUJICMD_HRS232_WRITE FUJICMD_MOUNT_ALL
#define FUJICMD_HRS232_STATUS FUJICMD_RANDOM_NUMBER
#define FUJICMD_HRS232_READ FUJICMD_GET_TIME
#define FUJICMD_HRS232_PUT FUJICMD_BASE64_ENCODE_INPUT
#define FUJICMD_HRS232_FORMAT_MEDIUM FUJICMD_GET_DEVICE3_FULLPATH
#define FUJICMD_HRS232_FORMAT FUJICMD_GET_DEVICE2_FULLPATH
#define FUJICMD_PARSE FUJICMD_PUT
#define FUJICMD_PERCOM_WRITE FUJICMD_OPEN
#define FUJICMD_AUTOANSWER FUJICMD_OPEN
#define FUJICMD_PERCOM_READ FUJICMD_BAUDRATELOCK
#define FUJICMD_SET_DUMP FUJICMD_SET_DESTINATION
#define FUJICMD_HSIO_INDEX FUJICMD_TYPE1_POLL
#define FUJICMD_LOAD_HANDLER FUJICMD_TELL
#define FUJICMD_LOAD_RELOCATOR FUJICMD_DELETE
#define FUJICMD_FORMAT FUJICMD_DELETE

#endif /* FUJI_COMMANDS_H */
