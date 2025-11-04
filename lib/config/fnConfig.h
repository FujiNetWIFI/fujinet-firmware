#ifndef _FN_CONFIG_H
#define _FN_CONFIG_H

#include <string>

#include "printer.h"
#include "../encrypt/crypt.h"
#include "../../include/debug.h"

#define MAX_HOST_SLOTS 8
#define MAX_MOUNT_SLOTS 8
#define MAX_PRINTER_SLOTS 4
#define MAX_TAPE_SLOTS 1
#define MAX_PB_SLOTS 16
#define MAX_WIFI_STORED 8

#define BASE_TAPE_SLOT 0x1A

#define HOST_SLOT_INVALID -1

#  define HSIO_INVALID_INDEX -1
#ifdef ESP_PLATFORM
#  define CONFIG_FILENAME "/fnconfig.ini"
#else /* ! ESP_PLATFORM */
#  define CONFIG_FILENAME "fnconfig.ini"
#  define SD_CARD_DIR "SD"
#  define WEB_SERVER_LISTEN_URL "http://0.0.0.0:8000"
#endif /* ESP_PLATFORM */

// Bus Over IP default port
#if defined(BUILD_ATARI)
// NetSIO default port for Atari
#  define CONFIG_DEFAULT_BOIP_PORT 9997
#elif defined(BUILD_COCO)
// DriveWire default port for CoCo
#  define CONFIG_DEFAULT_BOIP_PORT 65504
#else
// Dev relay over network, used by Apple
#  define CONFIG_DEFAULT_BOIP_PORT 1985
#endif

#ifdef BUILD_RS232
#define CONFIG_DEFAULT_RS232_BAUD 115200
#endif

#define CONFIG_FILEBUFFSIZE 2048

#define CONFIG_DEFAULT_SNTPSERVER "pool.ntp.org"

#define PHONEBOOK_CHAR_WIDTH 12


class fnConfig
{
public:
    enum host_types
    {
        HOSTTYPE_SD = 0,
        HOSTTYPE_TNFS,
        HOSTTYPE_INVALID
    };
    typedef host_types host_type_t;
    host_type_t host_type_from_string(const char *str);

    enum mount_modes
    {
        MOUNTMODE_READ = 0,
        MOUNTMODE_WRITE,
        MOUNTMODE_INVALID
    };
    typedef mount_modes mount_mode_t;
    mount_mode_t mount_mode_from_string(const char *str);

    enum mount_types
    {
        MOUNTTYPE_DISK = 0,
        MOUNTTYPE_TAPE
    };
    typedef mount_types mount_type_t;

#ifndef ESP_PLATFORM
    enum serial_command_pin
    {
        SERIAL_COMMAND_NONE = 0,
        SERIAL_COMMAND_DSR,
        SERIAL_COMMAND_CTS,
        SERIAL_COMMAND_RI,
        SERIAL_COMMAND_INVALID
    };
    serial_command_pin serial_command_from_string(const char *str);

    enum serial_proceed_pin
    {
        SERIAL_PROCEED_NONE = 0,
        SERIAL_PROCEED_DTR,
        SERIAL_PROCEED_RTS,
        SERIAL_PROCEED_INVALID
    };
    serial_proceed_pin serial_proceed_from_string(const char *str);
#endif

    // GENERAL
    std::string get_general_devicename() { return _general.devicename; };
#ifndef ESP_PLATFORM
    std::string get_general_label();
#endif
    int get_general_hsioindex() { return _general.hsio_index; };
    std::string get_general_timezone() { return _general.timezone; };
    bool get_general_rotation_sounds() { return _general.rotation_sounds; };
    std::string get_network_udpstream_host() { return _network.udpstream_host; };
    int get_network_udpstream_port() { return _network.udpstream_port; };
    bool get_network_udpstream_servermode() { return _network.udpstream_servermode; };
    bool get_general_config_enabled() { return _general.config_enabled; };
    void store_general_devicename(const char *devicename);
    void store_general_hsioindex(int hsio_index);
    void store_general_timezone(const char *timezone);
    void store_general_rotation_sounds(bool rotation_sounds);
    void store_general_config_enabled(bool config_enabled);
    void store_general_config_ng(bool config_ng);
    bool get_general_config_ng(){ return _general.config_ng; };
    std::string get_config_filename(){ return _general.config_filename; };
    void store_config_filename(const std::string &filename);
    bool get_general_boot_mode() { return _general.boot_mode; }
    void store_general_boot_mode(uint8_t boot_mode);
    void store_udpstream_host(const char host_ip[64]);
    void store_udpstream_port(int port);
    void store_udpstream_servermode(bool mode);
    bool get_general_fnconfig_spifs() { return _general.fnconfig_spifs; };
    void store_general_fnconfig_spifs(bool fnconfig_spifs);
    bool get_general_status_wait_enabled() { return _general.status_wait_enabled; }
    void store_general_status_wait_enabled(bool status_wait_enabled);
    void store_general_encrypt_passphrase(bool encrypt_passphrase);
    bool get_general_encrypt_passphrase();

    const char * get_network_sntpserver() { return _network.sntpserver; };

#ifndef ESP_PLATFORM
    std::string get_general_interface_url() { return _general.interface_url; };
    void store_general_interface_url(const char *url);
    std::string get_general_config_path() { return _general.config_file_path; };
    void store_general_config_path(const char *file_path);
    std::string get_general_SD_path() { return _general.SD_dir_path; };
    void store_general_SD_path(const char *dir_path);


    // SERIAL PORT
    std::string get_serial_port() { return _serial.port; };
    int get_serial_baud() { return _serial.baud; };
    serial_command_pin get_serial_command() { return _serial.command; };
    serial_proceed_pin get_serial_proceed() { return _serial.proceed; };
    void store_serial_port(const char *port);
    void store_serial_baud(int baud);
    void store_serial_command(serial_command_pin command_pin);
    void store_serial_proceed(serial_proceed_pin proceed_pin);
#endif

    // WIFI
    bool have_wifi_info() { return _wifi.ssid.empty() == false; };
    std::string get_wifi_ssid() { return _wifi.ssid; };
    std::string get_wifi_passphrase() {
        if (_general.encrypt_passphrase) {
            // crypt is an isomorphic operation, calling it when passphrase is encrypted will decrypt it.
            std::string cleartext = crypto.crypt(_wifi.passphrase);
            // Debug_printf("Decrypting passphrase >%s< for ssid >%s< with key >%s<, cleartext: >%s<\r\n", _wifi.passphrase.c_str(), _wifi.ssid.c_str(), crypto.getkey().c_str(), cleartext.c_str());
            return cleartext;
        } else {
            return _wifi.passphrase;
        }
    }
    void store_wifi_ssid(const char *ssid_octets, int num_octets);
    void store_wifi_passphrase(const char *passphrase_octets, int num_octets);
    void reset_wifi() { _wifi.ssid.clear(); _wifi.passphrase.clear(); };
    void store_wifi_enabled(bool status);
    bool get_wifi_enabled() { return _wifi.enabled; };

    std::string get_wifi_stored_ssid(int index) { return _wifi_stored[index].ssid; }
    std::string get_wifi_stored_passphrase(int index) { return _wifi_stored[index].passphrase; }
    bool get_wifi_stored_enabled(int index) { return _wifi_stored[index].enabled; }

    void store_wifi_stored_ssid(int index, const std::string &ssid); // { _wifi_stored[index].ssid = ssid; }
    void store_wifi_stored_passphrase(int index, const std::string &passphrase);
    void store_wifi_stored_enabled(int index, bool enabled); // { _wifi_stored[index].enabled = enabled; }

    // BLUETOOTH
    void store_bt_status(bool status);
    bool get_bt_status() { return _bt.bt_status; };
    void store_bt_baud(int baud);
    int get_bt_baud() { return _bt.bt_baud; };
    void store_bt_devname(const std::string &devname);
    std::string get_bt_devname() { return _bt.bt_devname; };

    // HOSTS
    std::string get_host_name(uint8_t num);
    host_type_t get_host_type(uint8_t num);
    void store_host(uint8_t num, const char *hostname, host_type_t type);
    void clear_host(uint8_t num);

    // PHONEBOOK SLOTS
    std::string get_pb_host_name(const char *pbnum);
    std::string get_pb_host_port(const char *pbnum);
    std::string get_pb_entry(uint8_t n);
    bool add_pb_number(const char *pbnum, const char *pbhost, const char *pbport);
    bool del_pb_number(const char *pbnum);
    void clear_pb(void);

    // MOUNTS
    std::string get_mount_path(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    mount_mode_t get_mount_mode(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    int get_mount_host_slot(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    void store_mount(uint8_t num, int hostslot, const char *path, mount_mode_t mode, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    void clear_mount(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);

    // PRINTERS
#ifdef PRINTER_CLASS
    PRINTER_CLASS::printer_type get_printer_type(uint8_t num);
    int get_printer_port(uint8_t num);
    void store_printer_enabled(bool printer_enabled);
    bool get_printer_enabled() { return _general.printer_enabled; };
    void store_printer_type(uint8_t num, PRINTER_CLASS::printer_type ptype);
    void store_printer_port(uint8_t num, int port);
#endif

    // MODEM
    void store_modem_enabled(bool modem_enabled);
    bool get_modem_enabled() { return _modem.modem_enabled; };
    void store_modem_sniffer_enabled(bool modem_sniffer_enabled);
    bool get_modem_sniffer_enabled() { return _modem.sniffer_enabled; };

    // CASSETTE
    bool get_cassette_buttons();
    bool get_cassette_pulldown();
    bool get_cassette_enabled();
    void store_cassette_buttons(bool button);
    void store_cassette_pulldown(bool pulldown);
    void store_cassette_enabled(bool cassette_enabled);

    // CPM
    std::string get_ccp_filename(){ return _cpm.ccp; };
    void store_ccp_filename(const std::string &filename);
    void store_cpm_enabled(bool cpm_enabled);
    bool get_cpm_enabled(){ return _cpm.cpm_enabled; };

    // ENABLE/DISABLE DEVICE SLOTS
    bool get_device_slot_enable_1();
    bool get_device_slot_enable_2();
    bool get_device_slot_enable_3();
    bool get_device_slot_enable_4();
    bool get_device_slot_enable_5();
    bool get_device_slot_enable_6();
    bool get_device_slot_enable_7();
    bool get_device_slot_enable_8();
    void store_device_slot_enable_1(bool enabled);
    void store_device_slot_enable_2(bool enabled);
    void store_device_slot_enable_3(bool enabled);
    void store_device_slot_enable_4(bool enabled);
    void store_device_slot_enable_5(bool enabled);
    void store_device_slot_enable_6(bool enabled);
    void store_device_slot_enable_7(bool enabled);
    void store_device_slot_enable_8(bool enabled);

    bool get_apetime_enabled();
    void store_apetime_enabled(bool enabled);
    bool get_pclink_enabled();
    void store_pclink_enabled(bool enabled);

    // BUS over IP
    bool get_boip_enabled() { return _boip.boip_enabled; } // used by Atari and CoCo
    std::string get_boip_host() { return _boip.host; }
    int get_boip_port() { return _boip.port; }
    void store_boip_enabled(bool enabled);
    void store_boip_host(const char *host);
    void store_boip_port(int port);

#ifdef BUILD_RS232
    // RS232
    int get_rs232_baud() { return _rs232.baud; }
    void store_rs232_baud(int baud);
#endif

#ifndef ESP_PLATFORM
    // BUS over Serial
    bool get_bos_enabled() { return _bos.bos_enabled; } // unused
    std::string get_bos_port_name() { return _bos.port_name; }
    int get_bos_baud() { return _bos.baud; }
    int get_bos_bits() { return _bos.bits; }
    int get_bos_parity() { return _bos.parity; }
    int get_bos_stop_bits() { return _bos.stop_bits; }
    int get_bos_flowcontrol() { return _bos.flowcontrol; }

    void store_bos_enabled(bool bos_enabled);
    void store_bos_port_name(char *port_name);
    void store_bos_baud(int baud);
    void store_bos_bits(int bits);
    void store_bos_parity(int parity);
    void store_bos_stop_bits(int stop_bits);
    void store_bos_flowcontrol(int flowcontrol);

#endif

    void load();
    void save();

    void mark_dirty() { _dirty = true; };

    fnConfig();

private:
    bool _dirty = false;

    int _read_line(std::stringstream &ss, std::string &line, char abort_if_starts_with = '\0');

    void _read_section_general(std::stringstream &ss);
    void _read_section_wifi(std::stringstream &ss);
    void _read_section_wifi_stored(std::stringstream &ss, int index);
    void _read_section_bt(std::stringstream &ss);
    void _read_section_network(std::stringstream &ss);
    void _read_section_host(std::stringstream &ss, int index);
    void _read_section_mount(std::stringstream &ss, int index);
    void _read_section_printer(std::stringstream &ss, int index);
    void _read_section_tape(std::stringstream &ss, int index);
    void _read_section_modem(std::stringstream &ss);
    void _read_section_cassette(std::stringstream &ss);
    void _read_section_phonebook(std::stringstream &ss, int index);
    void _read_section_cpm(std::stringstream &ss);
    void _read_section_device_enable(std::stringstream &ss);
    void _read_section_boip(std::stringstream &ss);
#ifndef ESP_PLATFORM
    void _read_section_serial(std::stringstream &ss);
    void _read_section_bos(std::stringstream &ss);
#endif
    void _read_section_rs232(std::stringstream &ss);

    enum section_match
    {
        SECTION_GENERAL,
        SECTION_WIFI,
        SECTION_WIFI_STORED,
        SECTION_BT,
        SECTION_HOST,
        SECTION_MOUNT,
        SECTION_PRINTER,
        SECTION_NETWORK,
        SECTION_TAPE,
        SECTION_MODEM,
        SECTION_CASSETTE,
        SECTION_PHONEBOOK,
        SECTION_CPM,
        SECTION_DEVICE_ENABLE,
        SECTION_BOIP,
#ifndef ESP_PLATFORM
        SECTION_SERIAL,
        SECTION_BOS,
#endif
#ifdef BUILD_RS232
        SECTION_RS232,
#endif
        SECTION_UNKNOWN
    };
    section_match _find_section_in_line(std::string &line, int &index);
    bool _split_name_value(std::string &line, std::string &name, std::string &value);

    const char * _host_type_names[HOSTTYPE_INVALID] = {
        "SD",
        "TNFS"
    };
    const char * _mount_mode_names[MOUNTMODE_INVALID] = {
        "r",
        "w"
    };

#ifndef ESP_PLATFORM
    const char * _serial_command_pin_names[SERIAL_COMMAND_INVALID] = {
        "none",
        "DSR",
        "CTS",
        "RI"
    };

    const char * _serial_proceed_pin_names[SERIAL_PROCEED_INVALID] = {
        "none",
        "DTR",
        "RTS"
    };
#endif

    struct host_info
    {
        host_type_t type = HOSTTYPE_INVALID;
        std::string name;
    };

    struct mount_info
    {
        int host_slot = HOST_SLOT_INVALID;
        mount_mode_t mode = MOUNTMODE_INVALID;
        std::string path;
    };

    struct printer_info
    {
#ifdef PRINTER_CLASS
        PRINTER_CLASS::printer_type type = PRINTER_CLASS::printer_type::PRINTER_INVALID;
#endif
        int port = 0;
    };

/*
     802.11 standard speficies a length 0 to 32 octets for SSID.
     No character encoding is specified, and all octet values are valid including
     zero. Although most SSIDs are treatred as ASCII strings, they are not subject
     to those limitations.
     We set asside 33 characters to allow for a zero terminator in a 32-char SSID
     and treat it as string instead of an array of arbitrary byte values.

     Similarly, the PSK (passphrase/password) is 64 octets.
     User-facing systems will typically take an 8 to 63 ASCII string and hash
     that into a 64 octet value. Although we're storing that ASCII string,
     we'll allow for 65 characters to allow for a zero-terminated 64 char
     string.
*/
    struct wifi_info
    {
        std::string ssid;
        std::string passphrase;
        bool enabled = true;
    };

    struct bt_info
    {
        bool bt_status = false;
        int bt_baud = 19200;
        std::string bt_devname = "SIO2BTFujiNet";
    };

    struct network_info
    {
        char sntpserver [40];
        char udpstream_host [64];
        int udpstream_port;
        bool udpstream_servermode;
    };

    struct general_info
    {
        std::string devicename = "FujiNet";
        int hsio_index = HSIO_INVALID_INDEX;
        std::string timezone;
        bool rotation_sounds = true;
        bool config_enabled = true;
        bool config_ng = false;
        std::string config_filename;
        int boot_mode = 0;
        bool fnconfig_spifs = true;
        bool status_wait_enabled = true;
        bool encrypt_passphrase = false;
#ifdef BUILD_ADAM
        bool printer_enabled = false; // Not by default.
#else
        bool printer_enabled = true;
#endif
#ifndef ESP_PLATFORM
        std::string interface_url = WEB_SERVER_LISTEN_URL; // default URL to serve web interface
        std::string config_file_path = CONFIG_FILENAME; // default path to load/save config file (program CWD)
        std::string SD_dir_path = SD_CARD_DIR; // default path to load/save config file
#endif
    };

    // "bus" over IP
    struct boip_info
    {
        bool boip_enabled = false;
#ifdef ESP_PLATFORM
        // CoCo: DriveWire server (listen) -> listen on all IPs by default
        // Atari: NetSIO hub (connect to)  -> hub host/IP must be specified
        std::string host = "";
#else
        // On PC, limit connections to/from local machine by default
        std::string host = "localhost";
#endif
        int port = CONFIG_DEFAULT_BOIP_PORT;
    };

#ifndef ESP_PLATFORM
    struct serial_info
    {
        std::string port;
        int baud = 57600; // Used by CoCo, ignored by Atari
        serial_command_pin command = SERIAL_COMMAND_DSR; // Used by Atari, ignored by CoCo
        serial_proceed_pin proceed = SERIAL_PROCEED_DTR; // Used by Atari, ignored by CoCo
    };

    // "bus" over serial
    struct bos_info
    {
        bool bos_enabled = false;
        std::string port_name = "COM1";
        int baud = 9600;
        int bits = 8;
        int parity = 0; // SP_PARITY_NONE
        int stop_bits = 1;
        int flowcontrol = 0; // SP_FLOWCONTROL_NONE
    };
#endif

#ifdef BUILD_RS232
    struct rs232_info
    {
        int baud = 115200;
    };
#endif

    struct modem_info
    {
        bool modem_enabled = true;
        bool sniffer_enabled = false;
    };

    struct cassette_info
    {
        bool cassette_enabled = true;
        bool pulldown = true;
        bool button = false;
    };

    struct cpm_info
    {
        bool cpm_enabled = true;
        std::string ccp;
    };

    struct device_enable_info
    {
        bool device_1_enabled = true;
        bool device_2_enabled = true;
        bool device_3_enabled = true;
        bool device_4_enabled = true;
        bool device_5_enabled = true;
        bool device_6_enabled = true;
        bool device_7_enabled = true;
        bool device_8_enabled = true;
        bool apetime = true;
        bool pclink = true;
    };

    struct phbook_info
    {
        std::string phnumber;
        std::string hostname;
        std::string port;
    };

    host_info _host_slots[MAX_HOST_SLOTS];
    mount_info _mount_slots[MAX_MOUNT_SLOTS];
    printer_info _printer_slots[MAX_PRINTER_SLOTS];
    mount_info _tape_slots[MAX_TAPE_SLOTS];
    wifi_info _wifi_stored[MAX_WIFI_STORED];

    wifi_info _wifi;
    bt_info _bt;
    network_info _network;
    general_info _general;
    modem_info _modem;
    cassette_info _cassette;
    boip_info _boip;
#ifndef ESP_PLATFORM
    serial_info _serial;
    bos_info _bos;
#endif
    cpm_info _cpm;
    device_enable_info _denable;
    phbook_info _phonebook_slots[MAX_PB_SLOTS];
#ifdef BUILD_RS232
    rs232_info _rs232;
#endif
};

extern fnConfig Config;

#endif //_FN_CONFIG_H
