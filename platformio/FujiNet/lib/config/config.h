#ifndef _FN_CONFIG_H
#define _FN_CONFIG_H

#include <string>

#include "../sio/printer.h"

#define MAX_HOST_SLOTS 8
#define MAX_MOUNT_SLOTS 8
#define MAX_PRINTER_SLOTS 4

#define HOST_SLOT_INVALID -1

class fnConfig
{
private:
    int _read_line(std::stringstream &ss, std::string &line, char abort_if_starts_with = '\0');

    void _read_section_wifi(std::stringstream &ss);
    void _read_section_host(std::stringstream &ss, int index);
    void _read_section_mount(std::stringstream &ss, int index);
    void _read_section_printer(std::stringstream &ss, int index);

    enum section_match {
        SECTION_WIFI,
        SECTION_HOST,
        SECTION_MOUNT,
        SECTION_PRINTER,
        SECTION_UNKNOWN
    };
    section_match _find_section_in_line(std::string &line, int &index);
    bool _split_name_value(std::string &line, std::string &name, std::string &value);

public:
    enum host_types
    {
        HOSTTYPE_SD = 0,
        HOSTTYPE_TNFS,
        HOSTTYPE_INVALID
    };
    typedef host_types host_type_t;

    const char * host_type_names[HOSTTYPE_INVALID] = {
        "SD",
        "TNFS"
    };

    host_type_t host_type_from_string(const char *str);

    struct host_info
    {
        host_type_t type = HOSTTYPE_INVALID;
        std::string name;
    };

    enum mount_modes
    {
        MOUNTMODE_READ = 0,
        MOUNTMODE_WRITE,
        MOUNTMODE_INVALID
    };
    typedef mount_modes mount_mode_t;

    const char * mount_mode_names[MOUNTMODE_INVALID] = {
        "r",
        "w"
    };

    mount_mode_t mount_mode_from_string(const char *str);

    struct mount_info
    {
        int host_slot = HOST_SLOT_INVALID;
        mount_mode_t mode = MOUNTMODE_READ;
        std::string path;
    };

    struct printer_info
    {
        sioPrinter::printer_type type = sioPrinter::printer_type::PRINTER_INVALID;
    };

    struct wifi_info
    {
        std::string ssid;
        std::string passphrase;
    };

    host_info host_slots[MAX_HOST_SLOTS];
    mount_info mount_slots[MAX_MOUNT_SLOTS];
    printer_info printer_slots[MAX_PRINTER_SLOTS];
    wifi_info wifi;

    void load();
    void save();
};

extern fnConfig Config;

#endif //_FN_CONFIG_H
