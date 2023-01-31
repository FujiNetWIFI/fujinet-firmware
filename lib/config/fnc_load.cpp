#include "fnConfig.h"
#include "fnFsSPIFFS.h"
#include "fnSystem.h"

#include <cstring>
#include <sstream>

#include "keys.h"
#include "utils.h"
#include "crypt.h"

#include "../../include/debug.h"

/* Load configuration data from SPIFFS. If no config file exists in SPIFFS,
   copy it from SD if a copy exists there.
*/
void fnConfig::load()
{
    Debug_println("fnConfig::load");

#if defined(NO_BUTTONS) || defined(BUILD_LYNX) || defined(BUILD_APPLE) || defined(BUILD_RS232)
    // Don't erase config if there are no buttons or on devices without Button B
#else
    // Clear the config file if key is currently pressed
    // This is the "Turn on while holding B button to reset Config" option.
    if (fnKeyManager.keyCurrentlyPressed(BUTTON_B))
    {
        Debug_println("fnConfig deleting configuration file and skipping SD check");

        // Tell the keymanager to ignore this keypress
        fnKeyManager.ignoreKeyPress(BUTTON_B);

        if (fnSPIFFS.exists(CONFIG_FILENAME))
            fnSPIFFS.remove(CONFIG_FILENAME);

        // full reset, so set us as not encrypting
        _general.encrypt_passphrase = false;
        
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
        case SECTION_CPM:
            _read_section_cpm(ss);
            break;
        case SECTION_PHONEBOOK: //Mauricio put this here to handle the phonebook
            _read_section_phonebook(ss, index);
            break;
        case SECTION_DEVICE_ENABLE: // Thom put this here to handle explicit device enables in adam
            _read_section_device_enable(ss);
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
