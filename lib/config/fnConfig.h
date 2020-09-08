#ifndef _FN_CONFIG_H
#define _FN_CONFIG_H

#include <string>

#include "../sio/printer.h"

#define MAX_HOST_SLOTS 8
#define MAX_MOUNT_SLOTS 8
#define MAX_PRINTER_SLOTS 4
#define MAX_TAPE_SLOTS 1

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
    std::string get_network_midimaze_host() { return _network.midimaze_host; };
    void store_general_devicename(const char *devicename);
    void store_general_hsioindex(int hsio_index);
    void store_general_timezone(const char *timezone);
    void store_midimaze_host(char host_ip[64]);

    const char * get_network_sntpserver() { return _network.sntpserver; };

    // WIFI
    bool have_wifi_info() { return _wifi.ssid.empty() == false; };
    std::string get_wifi_ssid() { return _wifi.ssid; };
    std::string get_wifi_passphrase() { return _wifi.passphrase; };
    void store_wifi_ssid(const char *ssid_octets, int num_octets);
    void store_wifi_passphrase(const char *passphrase_octets, int num_octets);
    void reset_wifi() { _wifi.ssid.clear(); _wifi.passphrase.clear(); };

    // HOSTS
    std::string get_host_name(uint8_t num);
    host_type_t get_host_type(uint8_t num);
    void store_host(uint8_t num, const char *hostname, host_type_t type);
    void clear_host(uint8_t num);

    // MOUNTS
    std::string get_mount_path(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    mount_mode_t get_mount_mode(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    int get_mount_host_slot(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    void store_mount(uint8_t num, int hostslot, const char *path, mount_mode_t mode, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);
    void clear_mount(uint8_t num, mount_type_t mounttype = mount_type_t::MOUNTTYPE_DISK);

    // PRINTERS
    sioPrinter::printer_type get_printer_type(uint8_t num);
    int get_printer_port(uint8_t num);
    void store_printer_type(uint8_t num, sioPrinter::printer_type ptype);
    void store_printer_port(uint8_t num, int port);


    void load();
    void save();

    fnConfig();

private:
    bool _dirty = false;

    int _read_line(std::stringstream &ss, std::string &line, char abort_if_starts_with = '\0');

    void _read_section_general(std::stringstream &ss);
    void _read_section_wifi(std::stringstream &ss);
    void _read_section_network(std::stringstream &ss);
    void _read_section_host(std::stringstream &ss, int index);
    void _read_section_mount(std::stringstream &ss, int index);
    void _read_section_printer(std::stringstream &ss, int index);
    void _read_section_tape(std::stringstream &ss, int index);    

    enum section_match {
        SECTION_GENERAL,
        SECTION_WIFI,
        SECTION_HOST,
        SECTION_MOUNT,
        SECTION_PRINTER,
        SECTION_NETWORK,
        SECTION_TAPE,
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
        sioPrinter::printer_type type = sioPrinter::printer_type::PRINTER_INVALID;
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
    };

    host_info _host_slots[MAX_HOST_SLOTS];
    mount_info _mount_slots[MAX_MOUNT_SLOTS];
    printer_info _printer_slots[MAX_PRINTER_SLOTS];
    mount_info _tape_slots[MAX_TAPE_SLOTS];

    wifi_info _wifi;
    network_info _network;
    general_info _general;
};

extern fnConfig Config;

#endif //_FN_CONFIG_H
