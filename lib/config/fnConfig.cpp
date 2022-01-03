#include <string>
#include <iostream>
#include <sstream>

#include "fnConfig.h"
#include "../FileSystem/fnFsSPIF.h"
#include "../FileSystem/fnFsSD.h"
#include "../hardware/keys.h"
#include "../utils/utils.h"
#include "../../include/debug.h"

#ifdef BUILD_ATARI
#include "modem-sniffer.h"
#include "sio/modem.h"
extern sioModem *sioR;
#endif

#define CONFIG_FILENAME "/fnconfig.ini"
#define CONFIG_FILEBUFFSIZE 2048

#define CONFIG_DEFAULT_SNTPSERVER "pool.ntp.org"

#define PHONEBOOK_CHAR_WIDTH 12 

fnConfig Config;

// Initialize some defaults
fnConfig::fnConfig()
{
    strlcpy(_network.sntpserver, CONFIG_DEFAULT_SNTPSERVER, sizeof(_network.sntpserver));
}

void fnConfig::store_midimaze_host(const char host_ip[64])
{
    strlcpy(_network.midimaze_host, host_ip, sizeof(_network.midimaze_host));
}

void fnConfig::store_general_devicename(const char *devicename)
{
    if (_general.devicename.compare(devicename) == 0)
        return;

    _general.devicename = devicename;
    _dirty = true;
}

void fnConfig::store_general_timezone(const char *timezone)
{
    if (_general.timezone.compare(timezone) == 0)
        return;

    _general.timezone = timezone;
    _dirty = true;
}

void fnConfig::store_general_rotation_sounds(bool rotation_sounds)
{
    if (_general.rotation_sounds == rotation_sounds)
        return;

    _general.rotation_sounds = rotation_sounds;
    _dirty = true;
}

void fnConfig::store_general_config_enabled(bool config_enabled)
{
    if (_general.config_enabled == config_enabled)
        return;

    _general.config_enabled = config_enabled;
    _dirty = true;
}
void fnConfig::store_general_status_wait_enabled(bool status_wait_enabled)
{
    if (_general.status_wait_enabled == status_wait_enabled)
        return;

    _general.status_wait_enabled = status_wait_enabled;
    _dirty = true;
}
void fnConfig::store_general_boot_mode(uint8_t boot_mode)
{
    if (_general.boot_mode == boot_mode)
        return;
    
    _general.boot_mode = boot_mode;
    _dirty = true;
}

void fnConfig::store_general_hsioindex(int hsio_index)
{
    if (_general.hsio_index == hsio_index)
        return;

    _general.hsio_index = hsio_index;
    _dirty = true;
}

void fnConfig::store_general_fnconfig_spifs(bool fnconfig_spifs)
{
    if (_general.fnconfig_spifs == fnconfig_spifs)
        return;

    _general.fnconfig_spifs = fnconfig_spifs;
    _dirty = true;
}

/* Replaces stored SSID with up to num_octets bytes, but stops if '\0' is reached
*/
void fnConfig::store_wifi_ssid(const char *ssid_octets, int num_octets)
{
    if (_wifi.ssid.compare(0, num_octets, ssid_octets) == 0)
        return;

    Debug_println("new SSID provided");

    _dirty = true;
    _wifi.ssid.clear();
    for (int i = 0; i < num_octets; i++)
    {
        if (ssid_octets[i] == '\0')
            break;
        else
            _wifi.ssid += ssid_octets[i];
    }
}

/* Replaces stored passphrase with up to num_octets bytes, but stops if '\0' is reached
*/
void fnConfig::store_wifi_passphrase(const char *passphrase_octets, int num_octets)
{
    if (_wifi.passphrase.compare(0, num_octets, passphrase_octets) == 0)
        return;
    _dirty = true;
    _wifi.passphrase.clear();
    for (int i = 0; i < num_octets; i++)
    {
        if (passphrase_octets[i] == '\0')
            break;
        else
            _wifi.passphrase += passphrase_octets[i];
    }
}

void fnConfig::store_bt_status(bool status)
{
    _bt.bt_status = status;
    _dirty = true;
}

void fnConfig::store_bt_baud(int baud)
{
    _bt.bt_baud = baud;
    _dirty = true;
}

void fnConfig::store_bt_devname(std::string devname)
{
    _bt.bt_devname = devname;
    _dirty = true;
}

std::string fnConfig::get_host_name(uint8_t num)
{
    if (num < MAX_HOST_SLOTS)
        return _host_slots[num].name;
    else
        return "";
}

fnConfig::host_type_t fnConfig::get_host_type(uint8_t num)
{
    if (num < MAX_HOST_SLOTS)
        return _host_slots[num].type;
    else
        return host_type_t::HOSTTYPE_INVALID;
}

void fnConfig::store_host(uint8_t num, const char *hostname, host_type_t type)
{
    if (num < MAX_HOST_SLOTS)
    {
        if (_host_slots[num].type == type && _host_slots[num].name.compare(hostname) == 0)
            return;
        _dirty = true;
        _host_slots[num].type = type;
        _host_slots[num].name = hostname;
    }
}

void fnConfig::clear_host(uint8_t num)
{
    if (num < MAX_HOST_SLOTS)
    {
        if (_host_slots[num].type == HOSTTYPE_INVALID && _host_slots[num].name.length() == 0)
            return;
        _dirty = true;
        _host_slots[num].type = HOSTTYPE_INVALID;
        _host_slots[num].name.clear();
    }
}

std::string fnConfig::get_mount_path(uint8_t num, mount_type_t mounttype)
{
    // Handle disk slots
    if (mounttype == MOUNTTYPE_DISK && num < MAX_MOUNT_SLOTS)
        return _mount_slots[num].path;

    // Handle tape slots
    if (mounttype == MOUNTTYPE_TAPE && num < MAX_TAPE_SLOTS)
        return _tape_slots[num].path;

    return "";
}

fnConfig::mount_mode_t fnConfig::get_mount_mode(uint8_t num, mount_type_t mounttype)
{
    // Handle disk slots
    if (mounttype == MOUNTTYPE_DISK && num < MAX_MOUNT_SLOTS)
        return _mount_slots[num].mode;

    // Handle tape slots
    if (mounttype == MOUNTTYPE_TAPE && num < MAX_TAPE_SLOTS)
        return _tape_slots[num].mode;

    return mount_mode_t::MOUNTMODE_INVALID;
}

int fnConfig::get_mount_host_slot(uint8_t num, mount_type_t mounttype)
{
    // Handle disk slots
    if (mounttype == MOUNTTYPE_DISK && num < MAX_MOUNT_SLOTS)
        return _mount_slots[num].host_slot;

    // Handle tape slots
    if (mounttype == MOUNTTYPE_TAPE && num < MAX_TAPE_SLOTS)
        return _tape_slots[num].host_slot;

    return HOST_SLOT_INVALID;
}

void fnConfig::store_mount(uint8_t num, int hostslot, const char *path, mount_mode_t mode, mount_type_t mounttype)
{
    // Handle disk slots
    if (mounttype == MOUNTTYPE_DISK && num < MAX_MOUNT_SLOTS)
    {
        if (_mount_slots[num].host_slot == hostslot && _mount_slots[num].mode == mode && _mount_slots[num].path.compare(path) == 0)
            return;
        _dirty = true;
        _mount_slots[num].host_slot = hostslot;
        _mount_slots[num].mode = mode;
        _mount_slots[num].path = path;

        return;
    }

    // Handle tape slots
    if (mounttype == MOUNTTYPE_TAPE && num < MAX_TAPE_SLOTS)
    {
        if (_tape_slots[num].host_slot == hostslot && _tape_slots[num].mode == mode && _tape_slots[num].path.compare(path) == 0)
            return;
        _dirty = true;
        _tape_slots[num].host_slot = hostslot;
        _tape_slots[num].mode = mode;
        _tape_slots[num].path = path;

        return;
    }
}

void fnConfig::clear_mount(uint8_t num, mount_type_t mounttype)
{
    // Handle disk slots
    if (mounttype == MOUNTTYPE_DISK && num < MAX_MOUNT_SLOTS)
    {
        if (_mount_slots[num].host_slot == HOST_SLOT_INVALID && _mount_slots[num].mode == MOUNTMODE_INVALID && _mount_slots[num].path.length() == 0)
            return;
        _dirty = true;
        _mount_slots[num].path.clear();
        _mount_slots[num].host_slot = HOST_SLOT_INVALID;
        _mount_slots[num].mode = MOUNTMODE_INVALID;
        return;
    }

    // Handle tape slots
    if (mounttype == MOUNTTYPE_TAPE && num < MAX_TAPE_SLOTS)
    {
        if (_tape_slots[num].host_slot == HOST_SLOT_INVALID && _tape_slots[num].mode == MOUNTMODE_INVALID && _tape_slots[num].path.length() == 0)
            return;
        _dirty = true;
        _tape_slots[num].path.clear();
        _tape_slots[num].host_slot = HOST_SLOT_INVALID;
        _tape_slots[num].mode = MOUNTMODE_INVALID;
        return;
    }
}

//The next section its to handle the phonebook entries
std::string fnConfig::get_pb_host_name(const char *pbnum)
{
    int i=0;
    while ( i<MAX_PB_SLOTS && ( _phonebook_slots[i].phnumber.compare(pbnum)!=0 ) ) 
        i++;
    
    //Number found
    if (i<MAX_PB_SLOTS)
        return _phonebook_slots[i].hostname;

    //Return empty (not found)
    return std::string();
}
std::string fnConfig::get_pb_host_port(const char *pbnum)
{
    int i=0;
    while ( i<MAX_PB_SLOTS && ( _phonebook_slots[i].phnumber.compare(pbnum)!=0 ) ) 
        i++;

    //Number found
    if (i<MAX_PB_SLOTS)
        return _phonebook_slots[i].port;

    //Return empty (not found)
    return std::string();
}
bool fnConfig::add_pb_number(const char *pbnum, const char *pbhost, const char *pbport)
{
    //Check maximum lenght of phone number
    if ( strlen(pbnum)>PHONEBOOK_CHAR_WIDTH )
        return false;

    int i=0;
    while (i<MAX_PB_SLOTS && !(_phonebook_slots[i].phnumber.empty()) )
        i++;
    
    //Empty found
    if (i<MAX_PB_SLOTS)
    {
        _phonebook_slots[i].phnumber = pbnum;
        _phonebook_slots[i].hostname = pbhost;
        _phonebook_slots[i].port = pbport;
        _dirty = true;
        save();
        return true;
    }
    return false;
    
}
bool fnConfig::del_pb_number(const char *pbnum)
{
    int i=0;
    while ( i<MAX_PB_SLOTS && ( _phonebook_slots[i].phnumber.compare(pbnum)!=0 ) ) 
        i++;

    //Number found
    if (i<MAX_PB_SLOTS)
    {
        _phonebook_slots[i].phnumber.clear();
        _phonebook_slots[i].hostname.clear();
        _phonebook_slots[i].port.clear();
        _dirty = true;
        save();
        return true;
    }
    //Not found
    return false;
}
void fnConfig::clear_pb(void)
{
    for (int i=0; i<MAX_PB_SLOTS; i++) 
    {
        _phonebook_slots[i].phnumber.clear();
        _phonebook_slots[i].hostname.clear();
        _phonebook_slots[i].port.clear();
    }
    save();
    _dirty = true;
}
std::string fnConfig::get_pb_entry(uint8_t n)
{
    if (_phonebook_slots[n].phnumber.empty())
        return std::string();
    std::string numberPadded =  _phonebook_slots[n].phnumber;
    numberPadded.append(PHONEBOOK_CHAR_WIDTH + 1 - numberPadded.length(), ' ');
    std::string pbentry =  numberPadded + _phonebook_slots[n].hostname + ":" + _phonebook_slots[n].port;
    return pbentry;
}
////End of phonebook handling

// Returns printer type stored in configuration for printer slot
PRINTER_CLASS::printer_type fnConfig::get_printer_type(uint8_t num)
{
    if (num < MAX_PRINTER_SLOTS)
        return _printer_slots[num].type;
    else
        return PRINTER_CLASS::printer_type::PRINTER_INVALID;
}

// Returns printer type stored in configuration for printer slot
int fnConfig::get_printer_port(uint8_t num)
{
    if (num < MAX_PRINTER_SLOTS)
        return _printer_slots[num].port;
    else
        return 0;
}

// Saves ENABLE or DISABLE printer
void fnConfig::store_printer_enabled(bool printer_enabled)
{
    if (_general.printer_enabled == printer_enabled)
        return;

    _general.printer_enabled = printer_enabled;
    _dirty = true;
}

// Saves printer type stored in configuration for printer slot
void fnConfig::store_printer_type(uint8_t num, PRINTER_CLASS::printer_type ptype)
{
    Debug_printf("store_printer_type %d, %d\n", num, ptype);
    if (num < MAX_PRINTER_SLOTS)
    {
        if (_printer_slots[num].type != ptype)
        {
            _dirty = true;
            _printer_slots[num].type = ptype;
        }
    }
}

// Saves printer port stored in configuration for printer slot
void fnConfig::store_printer_port(uint8_t num, int port)
{
    Debug_printf("store_printer_port %d, %d\n", num, port);
    if (num < MAX_PRINTER_SLOTS)
    {
        if (_printer_slots[num].port != port)
        {
            _dirty = true;
            _printer_slots[num].port = port;
        }
    }
}

// Saves ENABLE or DISABLE Modem
void fnConfig::store_modem_enabled(bool modem_enabled)
{
    if (_modem.modem_enabled == modem_enabled)
        return;

    _modem.modem_enabled = modem_enabled;
    _dirty = true;
}

// Saves ENABLE or DISABLE Modem Sniffer
void fnConfig::store_modem_sniffer_enabled(bool modem_sniffer_enabled)
{
#ifndef BUILD_ADAM
    ModemSniffer *modemSniffer = sioR->get_modem_sniffer();

    if (modem_sniffer_enabled)
    {
        if (!modemSniffer->getEnable())
            modemSniffer->setEnable(true);
    }
    else
        modemSniffer->setEnable(false);

    if (_modem.sniffer_enabled == modem_sniffer_enabled)
        return;

    _modem.sniffer_enabled = modem_sniffer_enabled;
    _dirty = true;
#endif /* BUILD_ADAM */
}

bool fnConfig::get_cassette_buttons()
{
    return _cassette.button;
}

bool fnConfig::get_cassette_pulldown()
{
    return _cassette.pulldown;
}

void fnConfig::store_cassette_buttons(bool button)
{
    if (_cassette.button != button)
    {
        _cassette.button = button;
        _dirty = true;
    }
}

void fnConfig::store_cassette_pulldown(bool pulldown)
{
    if (_cassette.pulldown != pulldown)
    {
        _cassette.pulldown = pulldown;
        _dirty = true;
    }
}

/* Save configuration data to SPIFFS. If SD is mounted, save a backup copy there.
*/
void fnConfig::save()
{

    Debug_println("fnConfig::save");

    if (!_dirty)
    {
        Debug_println("fnConfig::save not dirty, not saving");
        return;
    }

    // We're going to write a stringstream so that we have only one write to file at the end
    std::stringstream ss;

#define LINETERM "\r\n"

    // GENERAL
    ss << "[General]" LINETERM;
    ss << "devicename=" << _general.devicename << LINETERM;
    ss << "hsioindex=" << _general.hsio_index << LINETERM;
    ss << "rotationsounds=" << _general.rotation_sounds << LINETERM;
    ss << "configenabled=" << _general.config_enabled << LINETERM;
    ss << "boot_mode=" << _general.boot_mode << LINETERM;
    if (_general.timezone.empty() == false)
        ss << "timezone=" << _general.timezone << LINETERM;
    ss << "fnconfig_on_spifs=" << _general.fnconfig_spifs << LINETERM;
    ss << "status_wait_enabled=" << _general.status_wait_enabled << LINETERM;
    ss << "printer_enabled=" << _general.printer_enabled << LINETERM;

    ss << LINETERM;

    // WIFI
    ss << LINETERM << "[WiFi]" LINETERM;
    ss << "SSID=" << _wifi.ssid << LINETERM;
    // TODO: Encrypt passphrase!
    ss << "passphrase=" << _wifi.passphrase << LINETERM;

    // BLUETOOTH
    ss << LINETERM << "[Bluetooth]" LINETERM;
    ss << "devicename=" << _bt.bt_devname << LINETERM;
    ss << "enabled=" << _bt.bt_status << LINETERM;
    ss << "baud=" << _bt.bt_baud << LINETERM;

    // NETWORK
    ss << LINETERM << "[Network]" LINETERM;
    ss << "sntpserver=" << _network.sntpserver << LINETERM;

    // HOSTS
    int i;
    for (i = 0; i < MAX_HOST_SLOTS; i++)
    {
        if (_host_slots[i].type != HOSTTYPE_INVALID)
        {
            ss << LINETERM << "[Host" << (i + 1) << "]" LINETERM;
            ss << "type=" << _host_type_names[_host_slots[i].type] << LINETERM;
            ss << "name=" << _host_slots[i].name << LINETERM;
        }
    }

    // MOUNTS
    for (i = 0; i < MAX_MOUNT_SLOTS; i++)
    {
        if (_mount_slots[i].host_slot >= 0)
        {
            ss << LINETERM << "[Mount" << (i + 1) << "]" LINETERM;
            ss << "hostslot=" << (_mount_slots[i].host_slot + 1) << LINETERM; // Write host slot as 1-based
            ss << "path=" << _mount_slots[i].path << LINETERM;
            ss << "mode=" << _mount_mode_names[_mount_slots[i].mode] << LINETERM;
        }
    }

    // PRINTERS
    for (i = 0; i < MAX_PRINTER_SLOTS; i++)
    {
        if (_printer_slots[i].type != PRINTER_CLASS::printer_type::PRINTER_INVALID)
        {
            ss << LINETERM << "[Printer" << (i + 1) << "]" LINETERM;
            ss << "type=" << _printer_slots[i].type << LINETERM;
            ss << "port=" << (_printer_slots[i].port + 1) << LINETERM; // Write port # as 1-based
        }
    }

    // TAPES
    for (i = 0; i < MAX_TAPE_SLOTS; i++)
    {
        if (_tape_slots[i].host_slot >= 0)
        {
            ss << LINETERM << "[Tape" << (i + 1) << "]" LINETERM;
            ss << "hostslot=" << (_tape_slots[i].host_slot + 1) << LINETERM; // Write host slot as 1-based
            ss << "path=" << _tape_slots[i].path << LINETERM;
            ss << "mode=" << _mount_mode_names[_tape_slots[i].mode] << LINETERM;
        }
    }

    // MODEM
    ss << LINETERM << "[Modem]" << LINETERM;
    ss << "modem_enabled=" << _modem.modem_enabled << LINETERM;
    ss << "sniffer_enabled=" << _modem.sniffer_enabled << LINETERM;

    //PHONEBOOK
    for (i = 0; i < MAX_PB_SLOTS; i++)
    {
        if (!_phonebook_slots[i].phnumber.empty())
        {
            ss << LINETERM << "[Phonebook" << (i+1) << "]" LINETERM;
            ss << "number=" << _phonebook_slots[i].phnumber << LINETERM;
            ss << "host=" << _phonebook_slots[i].hostname << LINETERM;
            ss << "port=" << _phonebook_slots[i].port << LINETERM;
        }
    }

    // CASSETTE
    ss << LINETERM << "[Cassette]" << LINETERM;
    ss << "play_record=" << ((_cassette.button) ? "1 Record" : "0 Play") << LINETERM;
    ss << "pulldown=" << ((_cassette.pulldown) ? "1 Pulldown Resistor" : "0 B Button Press") << LINETERM;

    // Write the results out
    FILE *fout = NULL;
    if (fnConfig::get_general_fnconfig_spifs() == true) //only if spiffs is enabled
    {
        Debug_println("SPIFFS Config Storage: Enabled. Saving config to SPIFFS");
        if ( !(fout = fnSPIFFS.file_open(CONFIG_FILENAME, "w")))
        {
            Debug_println("Failed to Open config on SPIFFS");
            return;
        }
    }
    else
    {
        Debug_println("SPIFFS Config Storage: Disabled. Saving config to SD");
        if ( !(fout = fnSDFAT.file_open(CONFIG_FILENAME, "w")))
        {
            Debug_println("Failed to Open config on SD");
            return;
        }
    }
        std::string result = ss.str();
        size_t z = fwrite(result.c_str(), 1, result.length(), fout);
        (void)z; // Get around unused var
        Debug_printf("fnConfig::save wrote %d bytes\n", z);
        fclose(fout);
    
    _dirty = false;

    // Copy to SD if possible, only when wrote SPIFFS first 
    if (fnSDFAT.running() && fnConfig::get_general_fnconfig_spifs() == true)
    {
        Debug_println("Attemptiong config copy to SD");
        if (0 == fnSystem.copy_file(&fnSPIFFS, CONFIG_FILENAME, &fnSDFAT, CONFIG_FILENAME))
        {
            Debug_println("Failed to copy config to SD");
            return;
        }
    }
}

/* Load configuration data from SPIFFS. If no config file exists in SPIFFS,
   copy it from SD if a copy exists there.
*/
void fnConfig::load()
{
    Debug_println("fnConfig::load");

#ifndef NO_BUTTONS
    // Clear the config file if key is currently pressed
    if (fnKeyManager.keyCurrentlyPressed(BUTTON_B))
    {
        Debug_println("fnConfig deleting configuration file and skipping SD check");

        // Tell the keymanager to ignore this keypress
        fnKeyManager.ignoreKeyPress(BUTTON_B);

        if (fnSPIFFS.exists(CONFIG_FILENAME))
            fnSPIFFS.remove(CONFIG_FILENAME);

        _dirty = true; // We have a new config, so we treat it as needing to be saved
        return;
    }
#endif /* NO_BUTTONS */

    /*
Original behavior: read from SPIFFS first and only read from SD if nothing found on SPIFFS.

    // See if we have a file in SPIFFS
    if(false == fnSPIFFS.exists(CONFIG_FILENAME))
    {
        // See if we have a copy on SD (only copy from SD if we don't have a local copy)
        if(fnSDFAT.running() && fnSDFAT.exists(CONFIG_FILENAME))
        {
            Debug_println("Found copy of config file on SD - copying that to SPIFFS");
            if(0 == fnSystem.copy_file(&fnSDFAT, CONFIG_FILENAME, &fnSPIFFS, CONFIG_FILENAME))
            {
                Debug_println("Failed to copy config from SD");
                return; // No local copy and couldn't copy from SD - ABORT
            }
        }
        else
        {
            _dirty = true; // We have a new (blank) config, so we treat it as needing to be saved            
            return; // No local copy and no copy on SD - ABORT
        }
    }
*/
    /*
New behavior: copy from SD first if available, then read SPIFFS.
*/
    // See if we have a copy on SD load it to check if we should write to spiffs (only copy from SD if we don't have a local copy)
    FILE *fin = NULL; //declare fin
    if (fnSDFAT.running() && fnSDFAT.exists(CONFIG_FILENAME))
    {
        Debug_println("Load fnconfig.ini from SD");
        fin = fnSDFAT.file_open(CONFIG_FILENAME);
    }
    else
    {
        if (false == fnSPIFFS.exists(CONFIG_FILENAME))
        {
            _dirty = true; // We have a new (blank) config, so we treat it as needing to be saved
            Debug_println("No config found - starting fresh!");
            return; // No local copy - ABORT
        }
        Debug_println("Load fnconfig.ini from spiffs");
        fin = fnSPIFFS.file_open(CONFIG_FILENAME);
    }
    // Read INI file into buffer (for speed)
    // Then look for sections and handle each
    char *inibuffer = (char *)malloc(CONFIG_FILEBUFFSIZE);
    if (inibuffer == nullptr)
    {
        Debug_printf("Failed to allocate %d bytes to read config file\n", CONFIG_FILEBUFFSIZE);
        return;
    }
    int i = fread(inibuffer, 1, CONFIG_FILEBUFFSIZE - 1, fin);
    fclose(fin);

    Debug_printf("fnConfig::load read %d bytes from config file\n", i);

    if (i < 0)
    {
        Debug_println("Failed to read data from configuration file");
        free(inibuffer);
        return;
    }
    inibuffer[i] = '\0';
    // Put the data in a stringstream
    std::stringstream ss;
    ss << inibuffer;
    free(inibuffer);

    std::string line;
    while (_read_line(ss, line) >= 0)
    {
        int index = 0;
        switch (_find_section_in_line(line, index))
        {
        case SECTION_GENERAL:
            _read_section_general(ss);
            break;
        case SECTION_WIFI:
            _read_section_wifi(ss);
            break;
        case SECTION_BT:
            _read_section_bt(ss);
            break;
        case SECTION_NETWORK:
            _read_section_network(ss);
            break;
        case SECTION_HOST:
            _read_section_host(ss, index);
            break;
        case SECTION_MOUNT:
            _read_section_mount(ss, index);
            break;
        case SECTION_PRINTER:
            _read_section_printer(ss, index);
            break;
        case SECTION_TAPE: // Oscar put this here to handle server/path to CAS files
            _read_section_tape(ss, index);
            break;
        case SECTION_MODEM:
            _read_section_modem(ss);
            break;
        case SECTION_CASSETTE: //Jeff put this here to handle tape drive configuration (pulldown and play/record)
            _read_section_cassette(ss);
            break;
        case SECTION_PHONEBOOK: //Mauricio put this here to handle the phonebook
            _read_section_phonebook(ss, index);
            break;
        case SECTION_UNKNOWN:
            break;
        }
    }
    _dirty = false;

    if (fnConfig::get_general_fnconfig_spifs() == true) // Only if spiffs is enabled
    {
        if (true == fnSPIFFS.exists(CONFIG_FILENAME))
        {
            Debug_println("SPIFFS Config Storage: Enabled");
            FILE *fin = fnSPIFFS.file_open(CONFIG_FILENAME);
            char *inibuffer = (char *)malloc(CONFIG_FILEBUFFSIZE);
            if (inibuffer == nullptr)
            {
                Debug_printf("Failed to allocate %d bytes to read config file from SPIFFS\n", CONFIG_FILEBUFFSIZE);
                return;
            }
            int i = fread(inibuffer, 1, CONFIG_FILEBUFFSIZE - 1, fin);
            fclose(fin);
            Debug_printf("fnConfig::load read %d bytes from SPIFFS config file\n", i);
            if (i < 0)
            {
                Debug_println("Failed to read data from SPIFFS configuration file");
                free(inibuffer);
                return;
            }
            inibuffer[i] = '\0';
            // Put the data in a stringstream
            std::stringstream ss_ffs;
            ss_ffs << inibuffer;
            free(inibuffer);
            if (ss.str() != ss_ffs.str()) {
                Debug_println("Copying SD config file to SPIFFS");
                if (0 == fnSystem.copy_file(&fnSDFAT, CONFIG_FILENAME, &fnSPIFFS, CONFIG_FILENAME))
                {
                    Debug_println("Failed to copy config from SD");
                }
            }
            ss_ffs.str("");
            ss_ffs.clear(); // freeup some memory ;)
        }
        else
        {
            Debug_println("Config file dosn't exist on SPIFFS");
            Debug_println("Copying SD config file to SPIFFS");
            if (0 == fnSystem.copy_file(&fnSDFAT, CONFIG_FILENAME, &fnSPIFFS, CONFIG_FILENAME))
            {
                    Debug_println("Failed to copy config from SD");
            } 
        }
    }
}

void fnConfig::_read_section_general(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "devicename") == 0)
            {
                _general.devicename = value;
            }
            else if (strcasecmp(name.c_str(), "hsioindex") == 0)
            {
                int index = atoi(value.c_str());
                if (index >= 0 && index < 10)
                    _general.hsio_index = index;
            }
            else if (strcasecmp(name.c_str(), "timezone") == 0)
            {
                _general.timezone = value;
            }
            else if (strcasecmp(name.c_str(), "rotationsounds") == 0)
            {
                _general.rotation_sounds = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "configenabled") == 0)
            {
                _general.config_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "boot_mode") == 0)
            {
                int mode = atoi(value.c_str());
                _general.boot_mode = mode;
            }
            else if (strcasecmp(name.c_str(), "fnconfig_on_spifs") == 0)
            {
                _general.fnconfig_spifs = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "status_wait_enabled") == 0)
            {
                _general.status_wait_enabled = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "printer_enabled") == 0)
            {
                _general.printer_enabled = util_string_value_is_true(value);
            }
        }
    }
}

void fnConfig::_read_section_network(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "sntpserver") == 0)
            {
                strlcpy(_network.sntpserver, value.c_str(), sizeof(_network.sntpserver));
            }
        }
    }
}

void fnConfig::_read_section_wifi(std::stringstream &ss)
{
    // Throw out any existing data
    _wifi.ssid.clear();
    _wifi.passphrase.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "SSID") == 0)
            {
                _wifi.ssid = value;
            }
            else if (strcasecmp(name.c_str(), "passphrase") == 0)
            {
                _wifi.passphrase = value;
            }
        }
    }
}

void fnConfig::_read_section_bt(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "enabled") == 0)
            {
                if (strcasecmp(value.c_str(), "1") == 0)
                    _bt.bt_status = true;
                else
                    _bt.bt_status = false; 
            }
            else if (strcasecmp(name.c_str(), "baud") == 0)
            {
                _bt.bt_baud = stoi(value);
            }
            else if (strcasecmp(name.c_str(), "devicename") == 0)
            {
                _bt.bt_devname = value;
            }
        }
    }}

void fnConfig::_read_section_host(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    _host_slots[index].type = HOSTTYPE_INVALID;
    _host_slots[index].name.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "name") == 0)
            {
                _host_slots[index].name = value;
            }
            else if (strcasecmp(name.c_str(), "type") == 0)
            {
                _host_slots[index].type = host_type_from_string(value.c_str());
            }
        }
    }
}

void fnConfig::_read_section_mount(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    _mount_slots[index].host_slot = HOST_SLOT_INVALID;
    _mount_slots[index].mode = MOUNTMODE_INVALID;
    _mount_slots[index].path.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "hostslot") == 0)
            {
                int slot = atoi(value.c_str()) - 1;
                if (slot < 0 || slot >= MAX_HOST_SLOTS)
                    slot = HOST_SLOT_INVALID;
                _mount_slots[index].host_slot = slot;
                //Debug_printf("config mount %d hostslot=%d\n", index, slot);
            }
            else if (strcasecmp(name.c_str(), "mode") == 0)
            {
                _mount_slots[index].mode = mount_mode_from_string(value.c_str());
                //Debug_printf("config mount %d mode=%d (\"%s\")\n", index, _mount_slots[index].mode, value.c_str());
            }
            else if (strcasecmp(name.c_str(), "path") == 0)
            {
                _mount_slots[index].path = value;
                //Debug_printf("config mount %d path=\"%s\"\n", index, value.c_str());
            }
        }
    }
}

void fnConfig::_read_section_tape(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    _tape_slots[index].host_slot = HOST_SLOT_INVALID;
    _tape_slots[index].mode = MOUNTMODE_INVALID;
    _tape_slots[index].path.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "hostslot") == 0)
            {
                int slot = atoi(value.c_str()) - 1;
                if (slot < 0 || slot >= MAX_HOST_SLOTS)
                    slot = HOST_SLOT_INVALID;
                _mount_slots[index].host_slot = slot;
                //Debug_printf("config mount %d hostslot=%d\n", index, slot);
            }
            else if (strcasecmp(name.c_str(), "mode") == 0)
            {
                _mount_slots[index].mode = mount_mode_from_string(value.c_str());
                //Debug_printf("config mount %d mode=%d (\"%s\")\n", index, _mount_slots[index].mode, value.c_str());
            }
            else if (strcasecmp(name.c_str(), "path") == 0)
            {
                _mount_slots[index].path = value;
                //Debug_printf("config mount %d path=\"%s\"\n", index, value.c_str());
            }
        }
    }
}

void fnConfig::_read_section_printer(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    _printer_slots[index].type = PRINTER_CLASS::printer_type::PRINTER_INVALID;

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "type") == 0)
            {
                int type = atoi(value.c_str());
                if (type < 0 || type >= PRINTER_CLASS::printer_type::PRINTER_INVALID)
                    type = PRINTER_CLASS::printer_type::PRINTER_INVALID;

                _printer_slots[index].type = (PRINTER_CLASS::printer_type)type;
                //Debug_printf("config printer %d type=%d\n", index, type);
            }
            else if (strcasecmp(name.c_str(), "port") == 0)
            {
                int port = atoi(value.c_str()) - 1;
                if (port < 0 || port > 3)
                    port = 0;

                _printer_slots[index].port = port;
                //Debug_printf("config printer %d port=%d\n", index, port + 1);
            }
        }
    }
}

void fnConfig::_read_section_modem(std::stringstream &ss)
{
    std::string line;

    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "modem_enabled") == 0)
                _modem.modem_enabled = util_string_value_is_true(value);
            else if (strcasecmp(name.c_str(), "sniffer_enabled") == 0)
                _modem.sniffer_enabled = util_string_value_is_true(value);
        }
    }
}

void fnConfig::_read_section_cassette(std::stringstream &ss)
{
    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "play_record") == 0)
            {
                _cassette.button = util_string_value_is_true(value);
            }
            else if (strcasecmp(name.c_str(), "pulldown") == 0)
            {
                _cassette.pulldown = util_string_value_is_true(value);
            }
        }
    }
}

void fnConfig::_read_section_phonebook(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    _phonebook_slots[index].phnumber.clear();
    _phonebook_slots[index].hostname.clear();
    _phonebook_slots[index].port.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while (_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if (_split_name_value(line, name, value))
        {
            if (strcasecmp(name.c_str(), "number") == 0)
            {
                _phonebook_slots[index].phnumber = value;
            }
            else if (strcasecmp(name.c_str(), "host") == 0)
            {
                _phonebook_slots[index].hostname = value;
            }
            else if (strcasecmp(name.c_str(), "port") == 0)
            {
                _phonebook_slots[index].port = value;
            }
        }
    }
}

/*
Looks for [SectionNameX] where X is an integer
Returns which SectionName was found and sets index to X if X is an integer
*/
fnConfig::section_match fnConfig::_find_section_in_line(std::string &line, int &index)
{
    // Look for something in brackets
    size_t b1 = line.find_first_of('[');
    if (b1 != std::string::npos)
    {
        b1++; // skip the opening bracket
        size_t b2 = line.find_last_of(']');
        if (b2 != std::string::npos)
        {
            std::string s1 = line.substr(b1, b2 - b1);
            //Debug_printf("examining \"%s\"\n", s1.c_str());
            if (strncasecmp("Host", s1.c_str(), 4) == 0)
            {
                index = atoi((const char *)(s1.c_str() + 4)) - 1;
                if (index < 0 || index >= MAX_HOST_SLOTS)
                {
                    Debug_println("Invalid index value - discarding");
                    return SECTION_UNKNOWN;
                }
                //Debug_printf("Found HOST %d\n", index);
                return SECTION_HOST;
            }
            else if (strncasecmp("Mount", s1.c_str(), 5) == 0)
            {
                index = atoi((const char *)(s1.c_str() + 5)) - 1;
                if (index < 0 || index >= MAX_MOUNT_SLOTS)
                {
                    Debug_println("Invalid index value - discarding");
                    return SECTION_UNKNOWN;
                }
                //Debug_printf("Found MOUNT %d\n", index);
                return SECTION_MOUNT;
            }
            else if (strncasecmp("Printer", s1.c_str(), 7) == 0)
            {
                index = atoi((const char *)(s1.c_str() + 7) - 1);
                if (index < 0 || index >= MAX_PRINTER_SLOTS)
                {
                    Debug_println("Invalid index value - discarding");
                    return SECTION_UNKNOWN;
                }
                //Debug_printf("Found PRINTER %d\n", index);
                return SECTION_PRINTER;
            }
            else if (strncasecmp("WiFi", s1.c_str(), 4) == 0)
            {
                //Debug_printf("Found WIFI\n");
                return SECTION_WIFI;
            }
            else if (strncasecmp("Bluetooth", s1.c_str(), 9) == 0)
            {
                //Debug_printf("Found Bluetooth\n");
                return SECTION_BT;
            }
            else if (strncasecmp("General", s1.c_str(), 7) == 0)
            {
                // Debug_printf("Found General\n");
                return SECTION_GENERAL;
            }
            else if (strncasecmp("Network", s1.c_str(), 7) == 0)
            {
                // Debug_printf("Found Network\n");
                return SECTION_NETWORK;
            }
            else if (strncasecmp("Tape", s1.c_str(), 4) == 0)
            {
                index = atoi((const char *)(s1.c_str() + 4)) - 1;
                if (index < 0 || index >= MAX_TAPE_SLOTS)
                {
                    Debug_println("Invalid index value - discarding");
                    return SECTION_UNKNOWN;
                }
                // Debug_printf("Found Cassette\n");
                return SECTION_TAPE;
            }
            else if (strncasecmp("Cassette", s1.c_str(), 8) == 0)
            {
                return SECTION_CASSETTE;
            }
            else if (strncasecmp("Phonebook", s1.c_str(), 9) == 0)
            {
                index = atoi((const char *)(s1.c_str() + 9)) - 1;
                if (index < 0 || index >= MAX_PB_SLOTS)
                {
                    Debug_println("Invalid index value - discarding");
                    return SECTION_UNKNOWN;
                }
                //Debug_printf("Found Phonebook Entry %d\n", index);
                return SECTION_PHONEBOOK;
            }
            else if (strncasecmp("Modem", s1.c_str(), 8) == 0)
            {
                return SECTION_MODEM;
            }
        }
    }
    return SECTION_UNKNOWN;
}

fnConfig::host_type_t fnConfig::host_type_from_string(const char *str)
{
    int i = 0;
    for (; i < host_type_t::HOSTTYPE_INVALID; i++)
        if (strcasecmp(_host_type_names[i], str) == 0)
            break;
    return (host_type_t)i;
}

fnConfig::mount_mode_t fnConfig::mount_mode_from_string(const char *str)
{
    int i = 0;
    for (; i < mount_mode_t::MOUNTMODE_INVALID; i++)
        if (strcasecmp(_mount_mode_names[i], str) == 0)
            break;
    return (mount_mode_t)i;
}

bool fnConfig::_split_name_value(std::string &line, std::string &name, std::string &value)
{
    // Look for '='
    size_t eq = line.find_first_of('=');
    if (eq > 1)
    {
        name = line.substr(0, eq);
        util_string_trim(name);
        value = line.substr(eq + 1);
        util_string_trim(value);
        return true;
    }
    return false;
}

int fnConfig::_read_line(std::stringstream &ss, std::string &line, char abort_if_starts_with)
{
    line.erase();

    char c;
    size_t count = 0;
    size_t err = 0;
    bool iseof = false;
    bool have_read_non_whitespace = false;
    std::streampos linestart = ss.tellg();

    while ((iseof = ss.eof()) == false)
    {
        ss.read(&c, 1);

        // Consume the next character after \r if it's a \n
        if (c == '\r')
        {
            if (ss.peek() == '\n')
                ss.read(&c, 1);
            break;
        }
        if (c == '\n')
            break;

        // Rewind to start of line if we found the abort character
        if (have_read_non_whitespace == false && abort_if_starts_with != '\0' && abort_if_starts_with == c)
        {
            ss.seekg(linestart);
            err = -1;
            break;
        }
        if (have_read_non_whitespace == false)
            have_read_non_whitespace = (c != 32 && c != 9);

        line += c;
        count++;
    }

    return (iseof || err) ? -1 : count;
}
