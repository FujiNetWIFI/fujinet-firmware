#ifndef _FN_CONFIG_H
#define _FN_CONFIG_H

#include <string>

#ifdef BUILD_ATARI
#include "../device/sio/printer.h"
#define PRINTER_CLASS sioPrinter

#elif BUILD_CBM
//#include "../device/iec/printer.h"
//#define PRINTER_CLASS iecPrinter

#elif BUILD_ADAM
#include "../device/adamnet/printer.h"
#define PRINTER_CLASS adamPrinter
#endif


#define MAX_HOST_SLOTS 8
#define MAX_MOUNT_SLOTS 8
#define MAX_PRINTER_SLOTS 4
#define MAX_TAPE_SLOTS 1
#define MAX_PB_SLOTS 16

#define BASE_TAPE_SLOT 0x1A

#define HOST_SLOT_INVALID -1

#define HSIO_INVALID_INDEX -1

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

    enum mount_types
    {
        MOUNTTYPE_DISK = 0,
        MOUNTTYPE_TAPE
    };
    typedef mount_types mount_type_t;

    mount_mode_t mount_mode_from_string(const char *str);

    // GENERAL
    std::string get_general_devicename() { return _general.devicename; };
    int get_general_hsioindex() { return _general.hsio_index; };
    std::string get_general_timezone() { return _general.timezone; };
    bool get_general_rotation_sounds() { return _general.rotation_sounds; };
    std::string get_network_midimaze_host() { return _network.midimaze_host; };
    bool get_general_config_enabled() { return _general.config_enabled; };
    void store_general_devicename(const char *devicename);
    void store_general_hsioindex(int hsio_index);
    void store_general_timezone(const char *timezone);
    void store_general_rotation_sounds(bool rotation_sounds);
    void store_general_config_enabled(bool config_enabled);
    bool get_general_boot_mode() { return _general.boot_mode; }
    void store_general_boot_mode(uint8_t boot_mode);
    void store_midimaze_host(const char host_ip[64]);
    bool get_general_fnconfig_spifs() { return _general.fnconfig_spifs; };
    void store_general_fnconfig_spifs(bool fnconfig_spifs);
    bool get_general_status_wait_enabled() { return _general.status_wait_enabled; }
    void store_general_status_wait_enabled(bool status_wait_enabled);

    const char * get_network_sntpserver() { return _network.sntpserver; };

    // WIFI
    bool have_wifi_info() { return _wifi.ssid.empty() == false; };
    std::string get_wifi_ssid() { return _wifi.ssid; };
    std::string get_wifi_passphrase() { return _wifi.passphrase; };
    void store_wifi_ssid(const char *ssid_octets, int num_octets);
    void store_wifi_passphrase(const char *passphrase_octets, int num_octets);
    void reset_wifi() { _wifi.ssid.clear(); _wifi.passphrase.clear(); };

    // BLUETOOTH
    void store_bt_status(bool status);
    bool get_bt_status() { return _bt.bt_status; };
    void store_bt_baud(int baud);
    int get_bt_baud() { return _bt.bt_baud; };
    void store_bt_devname(std::string devname);
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
    void store_cassette_buttons(bool button);
    void store_cassette_pulldown(bool pulldown);

    // CPM
    std::string get_ccp_filename(){ return _cpm.ccp; };
    void store_ccp_filename(std::string filename);

    void load();
    void save();

    fnConfig();

private:
    bool _dirty = false;

    int _read_line(std::stringstream &ss, std::string &line, char abort_if_starts_with = '\0');

    void _read_section_general(std::stringstream &ss);
    void _read_section_wifi(std::stringstream &ss);
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

    enum section_match
    {
        SECTION_GENERAL,
        SECTION_WIFI,
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
        char midimaze_host [64];
    };

    struct general_info
    {
        std::string devicename = "fujinet";
        int hsio_index = HSIO_INVALID_INDEX;
        std::string timezone;
        bool rotation_sounds = true;
        bool config_enabled = true;
        int boot_mode = 0;
        bool fnconfig_spifs = true;
        bool status_wait_enabled = true;
        bool printer_enabled = true;
    };

    struct modem_info
    {
        bool modem_enabled = true;
        bool sniffer_enabled = false;
    };

    struct cassette_info
    {
        bool pulldown = false;
        bool button = false;
    };

    struct cpm_info
    {
        std::string ccp;
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

    wifi_info _wifi;
    bt_info _bt;
    network_info _network;
    general_info _general;
    modem_info _modem;
    cassette_info _cassette;
    cpm_info _cpm;

    phbook_info _phonebook_slots[MAX_PB_SLOTS];
};

extern fnConfig Config;

#endif //_FN_CONFIG_H
