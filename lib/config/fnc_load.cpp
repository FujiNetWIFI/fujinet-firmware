#include "fnConfig.h"
#include "fnSystem.h"

#include "fsFlash.h"

#include <cstring>
#include <sstream>
#include <sys/stat.h>

#include "keys.h"
#include "utils.h"
#include "crypt.h"

#include "../../include/debug.h"

/* Load configuration data from FLASH. If no config file exists in FLASH,
   copy it from SD if a copy exists there.
*/
void fnConfig::load()
{
#ifdef ESP_PLATFORM
    Debug_println("fnConfig::load");

#if defined(BUILD_ATARI) || defined(BUILD_ADAM)
    // Don't erase config if there are no buttons or on devices without Button B
    // Clear the config file if key is currently pressed
    // This is the "Turn on while holding B button to reset Config" option.
    if (fnKeyManager.keyCurrentlyPressed(BUTTON_B))
    {
        Debug_println("fnConfig deleting configuration file and skipping SD check");

        // Tell the keymanager to ignore this keypress
        fnKeyManager.ignoreKeyPress(BUTTON_B);

        if (fsFlash.exists(CONFIG_FILENAME))
            fsFlash.remove(CONFIG_FILENAME);

        // full reset, so set us as not encrypting
        _general.encrypt_passphrase = false;

        _dirty = true; // We have a new config, so we treat it as needing to be saved
        return;
    }

#endif /* BUILD_ATARI */

    /*
Original behavior: read from FLASH first and only read from SD if nothing found on FLASH.

    // See if we have a file in FLASH
    if(false == fsFlash.exists(CONFIG_FILENAME))
    {
        // See if we have a copy on SD (only copy from SD if we don't have a local copy)
        if(fnSDFAT.running() && fnSDFAT.exists(CONFIG_FILENAME))
        {
            Debug_println("Found copy of config file on SD - copying that to FLASH");
            if(0 == fnSystem.copy_file(&fnSDFAT, CONFIG_FILENAME, &fsFlash, CONFIG_FILENAME))
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
New behavior: copy from SD first if available, then read FLASH.
*/
    // See if we have a copy on SD load it to check if we should write to flash (only copy from SD if we don't have a local copy)
    FILE *fin = NULL; //declare fin
    if (fnSDFAT.running() && fnSDFAT.exists(CONFIG_FILENAME))
    {
        Debug_println("Load fnconfig.ini from SD");
        fin = fnSDFAT.file_open(CONFIG_FILENAME);
    }
    else
    {
        if (false == fsFlash.exists(CONFIG_FILENAME))
        {
            _dirty = true; // We have a new (blank) config, so we treat it as needing to be saved
            Debug_println("No config found - starting fresh!");
            return; // No local copy - ABORT
        }
        Debug_println("Load fnconfig.ini from flash");
        fin = fsFlash.file_open(CONFIG_FILENAME);
    }
// ESP_PLATFORM
 #else
// !ESP_PLATFORM
    Debug_printf("fnConfig::load \"%s\"\n", _general.config_file_path.c_str());
        
    struct stat st;
    if (stat(_general.config_file_path.c_str(), &st) < 0)
    {
        _dirty = true; // We have a new (blank) config, so we treat it as needing to be saved
        Debug_println("No config found - starting fresh!");
        return; // No local copy - ABORT
    }
    FILE *fin = fopen(_general.config_file_path.c_str(), FILE_READ_TEXT);
// !ESP_PLATFORM
 #endif

    if (fin == nullptr)
    {
        Debug_printf("Failed to open config file\n");
        return;
    }

    // Read INI file into buffer (for speed)
    // Then look for sections and handle each
    char *inibuffer = (char *)malloc(CONFIG_FILEBUFFSIZE);
    if (inibuffer == nullptr)
    {
        Debug_printf("Failed to allocate %d bytes to read config file\r\n", CONFIG_FILEBUFFSIZE);
        return;
    }
    int i = fread(inibuffer, 1, CONFIG_FILEBUFFSIZE - 1, fin);
    fclose(fin);

    Debug_printf("fnConfig::load read %d bytes from config file\r\n", i);

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
        case SECTION_WIFI_STORED:
            _read_section_wifi_stored(ss, index);
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
        case SECTION_CPM:
            _read_section_cpm(ss);
            break;
        case SECTION_PHONEBOOK: //Mauricio put this here to handle the phonebook
            _read_section_phonebook(ss, index);
            break;
        case SECTION_DEVICE_ENABLE: // Thom put this here to handle explicit device enables in adam
            _read_section_device_enable(ss);
            break;
        // Bus Over IP
        case SECTION_BOIP:
            _read_section_boip(ss);
            break;
#ifndef ESP_PLATFORM
        case SECTION_SERIAL:
            _read_section_serial(ss);
            break;
        // Bus Over Serial, for APPLE SmartPort over Serial via USB/Serial
        case SECTION_BOS:
            _read_section_bos(ss);
            break;
#endif
#ifdef BUILD_RS232
        case SECTION_RS232:
            _read_section_rs232(ss);
            break;
#endif
        case SECTION_UNKNOWN:
            break;
        }
    }

    _dirty = false;

#ifdef ESP_PLATFORM
    if (fnConfig::get_general_fnconfig_spifs() == true) // Only if flash is enabled
    {
        if (true == fsFlash.exists(CONFIG_FILENAME))
        {
            Debug_println("FLASH Config Storage: Enabled");
            FILE *fin = fsFlash.file_open(CONFIG_FILENAME);
            char *inibuffer = (char *)malloc(CONFIG_FILEBUFFSIZE);
            if (inibuffer == nullptr)
            {
                Debug_printf("Failed to allocate %d bytes to read config file from FLASH\r\n", CONFIG_FILEBUFFSIZE);
                return;
            }
            int i = fread(inibuffer, 1, CONFIG_FILEBUFFSIZE - 1, fin);
            fclose(fin);
            Debug_printf("fnConfig::load read %d bytes from FLASH config file\r\n", i);
            if (i < 0)
            {
                Debug_println("Failed to read data from FLASH configuration file");
                free(inibuffer);
                return;
            }
            inibuffer[i] = '\0';
            // Put the data in a stringstream
            std::stringstream ss_ffs;
            ss_ffs << inibuffer;
            free(inibuffer);
            if (ss.str() != ss_ffs.str()) {
                Debug_println("Copying SD config file to FLASH");
                if (0 == fnSystem.copy_file(&fnSDFAT, CONFIG_FILENAME, &fsFlash, CONFIG_FILENAME))
                {
                    Debug_println("Failed to copy config from SD");
                }
            }
            ss_ffs.str("");
            ss_ffs.clear(); // freeup some memory ;)
        }
        else
        {
            Debug_println("Config file dosn't exist on FLASH");
            Debug_println("Copying SD config file to FLASH");
            if (0 == fnSystem.copy_file(&fnSDFAT, CONFIG_FILENAME, &fsFlash, CONFIG_FILENAME))
            {
                    Debug_println("Failed to copy config from SD");
            } 
        }
    }
#endif // ESP_PLATFORM
}
