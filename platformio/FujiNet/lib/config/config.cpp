#include <string>
#include <iostream>
#include <sstream>

#include "config.h"
#include "../../include/debug.h"

#include <SPIFFS.h>
#include <SD.h>

#include "../utils/utils.h"
#include "../hardware/keys.h"

#define CONFIG_FILENAME "/fnconfig.ini"
#define CONFIG_FILEBUFFSIZE 2048

fnConfig Config;


fnConfig::host_type_t fnConfig::host_type_from_string(const char *str)
{
    int i = 0;
    for(;i < host_type_t::HOSTTYPE_INVALID; i++)
        if(strcasecmp(host_type_names[i], str) == 0)
            break;
    return (host_type_t) i;
}

fnConfig::mount_mode_t fnConfig::mount_mode_from_string(const char *str)
{
    int i = 0;
    for(;i < mount_mode_t::MOUNTMODE_INVALID; i++)
        if(strcasecmp(mount_mode_names[i], str) == 0)
            break;
    return (mount_mode_t) i;
}

/* Load configuration data from SPIFFS. If no config file exists in SPIFFS,
   copy it from SD if a copy exists there.
*/
void fnConfig::load()
{
#ifdef DEBUG
    Debug_println("fnConfig::load");
#endif

    // Clear the config file if key is currently pressed
    if(KeyManager::keyCurrentlyPressed(OTHER_KEY))
    {
        #ifdef DEBUG
        Debug_println("fnConfig deleting configuration file and skipping SD check");
        #endif
        if(SPIFFS.exists(CONFIG_FILENAME))
            SPIFFS.remove(CONFIG_FILENAME);
        return;
    }

    // See if we have a file in SPIFFS
    if(false == SPIFFS.exists(CONFIG_FILENAME))
    {
        // See if we have a copy on SD (only copy from SD if we don't have a local copy)
        if(SD.cardType() != CARD_NONE && SD.exists(CONFIG_FILENAME))
        {
            #ifdef DEBUG
            Debug_println("Found copy of config file on SD - copying that to SPIFFS");
            #endif
            if(0 ==fnSystem.copy_file(&SD, CONFIG_FILENAME, &SPIFFS, CONFIG_FILENAME))
            {
                #ifdef DEBUG
                Debug_println("Failed to copy config from SD");
                #endif
                return; // No local copy and couldn't copy from SD - ABORT
            }
        }
        else
        {
            return; // No local copy and no copy on SD - ABORT
        }
    }

    // Read INI file into buffer (for speed)
    // Then look for sections and handle each
    File fin = SPIFFS.open(CONFIG_FILENAME);
    char *inibuffer = (char *)malloc(CONFIG_FILEBUFFSIZE);
    if(inibuffer == nullptr)
    {
        #ifdef DEBUG
        Debug_printf("Failed to allocate %d bytes to read config file\n", CONFIG_FILEBUFFSIZE);
        #endif
        return;
    }
    int i = fin.read((uint8_t *)inibuffer, CONFIG_FILEBUFFSIZE-1);
    fin.close();
    if(i < 0)
    {
        #ifdef DEBUG
        Debug_println("Failed to read data from configuration file");
        #endif
        free(inibuffer);
        return;
    }
    inibuffer[i] = '\0';
    // Put the data in a stringstream
    std::stringstream ss;
    ss << inibuffer;
    free(inibuffer);
    
    std::string line;
    while(_read_line(ss, line) >= 0)
    {
        int index = 0;
        switch(_find_section_in_line(line, index))
        {
        case SECTION_WIFI:
            _read_section_wifi(ss);
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
        case SECTION_UNKNOWN:
            break;
        }
    }

}

void fnConfig::_read_section_wifi(std::stringstream &ss)
{
    // Throw out any existing data
    wifi.ssid.clear();
    wifi.passphrase.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while(_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if(_split_name_value(line, name, value))
        {
            if(strcasecmp(name.c_str(), "SSID") == 0)
            {
                wifi.ssid = value;
                #ifdef DEBUG
                Debug_printf("config wifi=\"%s\"\n", value.c_str());
                #endif
            }
            else if (strcasecmp(name.c_str(), "passphrase") == 0)
            {
                wifi.passphrase = value;
                #ifdef DEBUG
                Debug_printf("config passphrase=\"%s\"\n", value.c_str());
                #endif
            }
        }
    }
}

void fnConfig::_read_section_host(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    host_slots[index].type = HOSTTYPE_INVALID;
    host_slots[index].name.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while(_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if(_split_name_value(line, name, value))
        {
            if(strcasecmp(name.c_str(), "name") == 0)
            {
                host_slots[index].name = value;
                #ifdef DEBUG
                Debug_printf("config host %d name=\"%s\"\n", index, value.c_str());
                #endif
            }
            else if (strcasecmp(name.c_str(), "type") == 0)
            {
                host_slots[index].type = host_type_from_string(value.c_str());
                #ifdef DEBUG
                Debug_printf("config host %d type=%d (\"%s\")\n", index, host_slots[index].type, value.c_str());
                #endif
            }
        }
    }
}

void fnConfig::_read_section_mount(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    mount_slots[index].host_slot = HOST_SLOT_INVALID;
    mount_slots[index].mode = MOUNTMODE_INVALID;
    mount_slots[index].path.clear();

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while(_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if(_split_name_value(line, name, value))
        {
            if(strcasecmp(name.c_str(), "hostslot") == 0)
            {
                int slot = atoi(value.c_str()) -1;
                if(slot < 0 || slot >= MAX_HOST_SLOTS)
                    slot = HOST_SLOT_INVALID;
                mount_slots[index].host_slot = slot;
                #ifdef DEBUG
                Debug_printf("config mount %d hostslot=%d\n", index, slot);
                #endif
            }
            else if (strcasecmp(name.c_str(), "mode") == 0)
            {
                mount_slots[index].mode = mount_mode_from_string(value.c_str());
                #ifdef DEBUG
                Debug_printf("config mount %d mode=%d (\"%s\")\n", index, mount_slots[index].mode, value.c_str());
                #endif
            }
            else if (strcasecmp(name.c_str(), "path") == 0)
            {
                mount_slots[index].path = value;
                #ifdef DEBUG
                Debug_printf("config mount %d path=\"%s\"\n", index, value.c_str());
                #endif
            }
        }
    }
}

void fnConfig::_read_section_printer(std::stringstream &ss, int index)
{
    // Throw out any existing data for this index
    printer_slots[index].type = sioPrinter::printer_type::PRINTER_INVALID;

    std::string line;
    // Read lines until one starts with '[' which indicates a new section
    while(_read_line(ss, line, '[') >= 0)
    {
        std::string name;
        std::string value;
        if(_split_name_value(line, name, value))
        {
            if(strcasecmp(name.c_str(), "type") == 0)
            {
                int type = atoi(value.c_str());
                if(type < 0 || type >= sioPrinter::printer_type::PRINTER_INVALID)
                    type = sioPrinter::printer_type::PRINTER_INVALID;

                printer_slots[index].type = (sioPrinter::printer_type)type;
                #ifdef DEBUG
                Debug_printf("config printer %d type=%d\n", index, type);
                #endif
            }
        }
    }
}

bool fnConfig::_split_name_value(std::string &line, std::string &name, std::string &value)
{
    // Look for '='
    //Debug_printf("_split_name_value \"%s\"\n", line.c_str());
    size_t eq = line.find_first_of('=');
    if(eq > 1)
    {
        name = line.substr(0, eq);
        util_trim(name);
        //Debug_printf("name \"%s\"\n", name.c_str());
        value = line.substr(eq+1);
        util_trim(value);
        //Debug_printf("value \"%s\"\n", value.c_str());        
        return true;
    }
    return false;
}

/*
Looks for [SectionNameX] where X is an integer
Returns which SectionName was found and sets index to X if X is an integer
*/
fnConfig::section_match fnConfig::_find_section_in_line(std::string &line, int &index)
{
    // Look for soemthing in brackets
    size_t b1 = line.find_first_of('[');
    if(b1 != std::string::npos)
    {
        b1++; // skip the opening bracket
        size_t b2 = line.find_last_of(']');
        if(b2 != std::string::npos)
        {
            std::string s1 = line.substr(b1,b2-b1);
            Debug_printf("examining \"%s\"\n", s1.c_str());
            if(strncasecmp("Host", s1.c_str(), 4) == 0)
            {
                index = atoi((const char *)(s1.c_str()+4)) -1;
                if(index < 0 || index >= MAX_HOST_SLOTS)
                {
                    #ifdef DEBUG
                    Debug_println("Invalid index value - discarding");
                    #endif                    
                    return SECTION_UNKNOWN;
                }
                Debug_printf("Found HOST %d\n", index);
                return SECTION_HOST;
            }
            else if(strncasecmp("Mount", s1.c_str(), 5) == 0)
            {
                index = atoi((const char *)(s1.c_str()+5)) -1;
                if(index < 0 || index >= MAX_MOUNT_SLOTS)
                {
                    #ifdef DEBUG
                    Debug_println("Invalid index value - discarding");
                    #endif                    
                    return SECTION_UNKNOWN;
                }
                Debug_printf("Found MOUNT %d\n", index);
                return SECTION_MOUNT;
            }
            else if(strncasecmp("Printer", s1.c_str(), 7) == 0)
            {
                index = atoi((const char *)(s1.c_str()+7) -1);
                if(index < 0 || index >= MAX_PRINTER_SLOTS)
                {
                    #ifdef DEBUG
                    Debug_println("Invalid index value - discarding");
                    #endif                    
                    return SECTION_UNKNOWN;
                }
                Debug_printf("Found PRINTER %d\n", index);
                return SECTION_PRINTER;
            } else if (strncasecmp("WiFi", s1.c_str(), 4) == 0)
            {
                Debug_printf("Found WIFI\n");
                return SECTION_WIFI;
            }
        }
    }
    return SECTION_UNKNOWN;
}


/* Save configuration data to SPIFFS. If SD is mounted, save a backup copy there.
*/
void fnConfig::save()
{
#ifdef DEBUG
    Debug_println("fnConfig::save");
#endif

    // We're going to write a stringstream so that we have only one write to file at the end
    std::stringstream ss;

#define LINETERM "\r\n"

    // WIFI
    ss << "[WiFi]" LINETERM;
    ss << "SSID=" << wifi.ssid << LINETERM;
    // TODO: Encrypt passphrase!
    ss << "passpharse=" << wifi.passphrase << LINETERM;

    // HOSTS
    int i;
    for(i=0; i < MAX_HOST_SLOTS; i++)
    {
        if(host_slots[i].type != HOSTTYPE_INVALID)
        {
            ss << LINETERM << "[Host" << (i+1) << "]" LINETERM;
            ss << "type=" << host_type_names[host_slots[i].type] << LINETERM;
            ss << "name=" << host_slots[i].name << LINETERM;
        }
    }
    
    // MOUNTS
    for(i=0; i < MAX_MOUNT_SLOTS; i++)
    {
        if(mount_slots[i].host_slot >= 0)
        {
            ss << LINETERM << "[Mount" << (i+1) << "]" LINETERM;
            ss << "hostslot=" << (mount_slots[i].host_slot + 1) << LINETERM; // Write host slot as 1-based
            ss << "path=" << mount_slots[i].path << LINETERM;
            ss << "mode=" << mount_mode_names[mount_slots[i].mode] << LINETERM;
        }
    }

    // PRINTERS
    for(i=0; i < MAX_PRINTER_SLOTS; i++)
    {
        if(printer_slots[i].type != sioPrinter::printer_type::PRINTER_INVALID)
        {
            ss << LINETERM << "[Printer" << (i+1) << "]" LINETERM;
            ss << "type=" << printer_slots[i].type << LINETERM;
        }
    }

    // Write the results out
    File fout = SPIFFS.open(CONFIG_FILENAME, "w");
    std::string result = ss.str();
    fout.write((uint8_t *)result.c_str(), result.length());
    fout.close();

    // Copy to SD if possible
    if(SD.cardType() != CARD_NONE)
    {
        Debug_println("Attemptiong config copy to SD");
        if(0 ==fnSystem.copy_file(&SPIFFS, CONFIG_FILENAME, &SD, CONFIG_FILENAME))
        {
            #ifdef DEBUG
            Debug_println("Failed to copy config to SD");
            #endif
            return;
        }
    }
}


int fnConfig::_read_line(std::stringstream &ss, std::string &line, char abort_if_starts_with)
{
    char c;
    size_t z = 0;
    size_t err = 0;
    bool iseof = false;

    line.erase();
    bool have_read_non_whitespace = false;

    std::streampos p = ss.tellg();

    while((iseof = ss.eof()) == false)
    {
        ss.read(&c, 1);

        // Consume the next character after \r if it's a \n
        if(c == '\r')
        {
            if(ss.peek() == '\n')
                ss.read(&c, 1);
            break;
        }
        if(c == '\n')
            break;

        // Rewind to start of line if we found the abort character
        if(have_read_non_whitespace == false && abort_if_starts_with != '\0' && abort_if_starts_with == c)
        {
            ss.seekg(p);
            err = -1;
            break;
        }
        if(have_read_non_whitespace == false)
            have_read_non_whitespace = (c != 32 && c != 9);

        line += c;
    }

    return (iseof || err != 0) ? -1 : z;
}

