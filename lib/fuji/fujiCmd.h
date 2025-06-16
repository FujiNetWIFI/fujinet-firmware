#ifndef _FUJI_CMD_
#define _FUJI_CMD_
/*
 * Fuji Device Command Definitions
 */

#define FUJICMD_RESET                      0xFF
#define FUJICMD_GET_SSID                   0xFE
#define FUJICMD_SCAN_NETWORKS              0xFD
#define FUJICMD_GET_SCAN_RESULT            0xFC
#define FUJICMD_SET_SSID                   0xFB
#define FUJICMD_GET_WIFISTATUS             0xFA
#define FUJICMD_MOUNT_HOST                 0xF9
#define FUJICMD_MOUNT_IMAGE                0xF8
#define FUJICMD_OPEN_DIRECTORY             0xF7
#define FUJICMD_READ_DIR_ENTRY             0xF6
#define FUJICMD_CLOSE_DIRECTORY            0xF5
#define FUJICMD_READ_HOST_SLOTS            0xF4
#define FUJICMD_WRITE_HOST_SLOTS           0xF3
#define FUJICMD_READ_DEVICE_SLOTS          0xF2
#define FUJICMD_WRITE_DEVICE_SLOTS         0xF1
#define FUJICMD_ENABLE_UDPSTREAM           0xF0
#define FUJICMD_GET_WIFI_ENABLED           0xEA
#define FUJICMD_SET_BAUDRATE               0xEB
#define FUJICMD_UNMOUNT_IMAGE              0xE9
#define FUJICMD_GET_ADAPTERCONFIG          0xE8
#define FUJICMD_NEW_DISK                   0xE7
#define FUJICMD_UNMOUNT_HOST               0xE6
#define FUJICMD_GET_DIRECTORY_POSITION     0xE5
#define FUJICMD_SET_DIRECTORY_POSITION     0xE4
#define FUJICMD_SET_HSIO_INDEX             0xE3
#define FUJICMD_SET_DEVICE_FULLPATH        0xE2
#define FUJICMD_SET_HOST_PREFIX            0xE1
#define FUJICMD_GET_HOST_PREFIX            0xE0
#define FUJICMD_SET_SIO_EXTERNAL_CLOCK     0xDF
#define FUJICMD_WRITE_APPKEY               0xDE
#define FUJICMD_READ_APPKEY                0xDD
#define FUJICMD_OPEN_APPKEY                0xDC
#define FUJICMD_CLOSE_APPKEY               0xDB
#define FUJICMD_GET_DEVICE_FULLPATH        0xDA
#define FUJICMD_CONFIG_BOOT                0xD9
#define FUJICMD_COPY_FILE                  0xD8
#define FUJICMD_MOUNT_ALL                  0xD7
#define FUJICMD_SET_BOOT_MODE              0xD6
#define FUJICMD_ENABLE_DEVICE              0xD5
#define FUJICMD_DISABLE_DEVICE             0xD4
#define FUJICMD_RANDOM_NUMBER              0xD3
#define FUJICMD_GET_TIME                   0xD2
#define FUJICMD_DEVICE_ENABLE_STATUS       0xD1
#define FUJICMD_BASE64_ENCODE_INPUT        0xD0
#define FUJICMD_BASE64_ENCODE_COMPUTE      0xCF
#define FUJICMD_BASE64_ENCODE_LENGTH       0xCE
#define FUJICMD_BASE64_ENCODE_OUTPUT       0xCD
#define FUJICMD_BASE64_DECODE_INPUT        0xCC
#define FUJICMD_BASE64_DECODE_COMPUTE      0xCB
#define FUJICMD_BASE64_DECODE_LENGTH       0xCA
#define FUJICMD_BASE64_DECODE_OUTPUT       0xC9
#define FUJICMD_HASH_INPUT                 0xC8
#define FUJICMD_HASH_COMPUTE               0xC7
#define FUJICMD_HASH_LENGTH                0xC6
#define FUJICMD_HASH_OUTPUT                0xC5
#define FUJICMD_GET_ADAPTERCONFIG_EXTENDED 0xC4
#define FUJICMD_HASH_COMPUTE_NO_CLEAR      0xC3
#define FUJICMD_HASH_CLEAR                 0xC2
#define FUJICMD_GET_HEAP                   0xC1
#define FUJICMD_QRCODE_OUTPUT              0xBF
#define FUJICMD_QRCODE_LENGTH              0xBE
#define FUJICMD_QRCODE_ENCODE              0xBD
#define FUJICMD_QRCODE_INPUT               0xBC
#define FUJICMD_GET_DEVICE8_FULLPATH       0xA7
#define FUJICMD_GET_DEVICE7_FULLPATH       0xA6
#define FUJICMD_GET_DEVICE6_FULLPATH       0xA5
#define FUJICMD_GET_DEVICE5_FULLPATH       0xA4
#define FUJICMD_GET_DEVICE4_FULLPATH       0xA3
#define FUJICMD_GET_DEVICE3_FULLPATH       0xA2
#define FUJICMD_GET_DEVICE2_FULLPATH       0xA1
#define FUJICMD_GET_DEVICE1_FULLPATH       0xA0
#define FUJICMD_STATUS                     0x53
#define FUJICMD_HSIO_INDEX                 0x3F
#define FUJICMD_SEND_ERROR                 0x02
#define FUJICMD_SEND_RESPONSE              0x01
#define FUJICMD_DEVICE_READY               0x00

#endif
