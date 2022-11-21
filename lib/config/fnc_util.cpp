#include "fnConfig.h"
#include <cstring>
#include <sstream>
#include "../../include/debug.h"
#include "utils.h"

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
            else if (strncasecmp("CPM", s1.c_str(), 8) == 0)
            {
                return SECTION_CPM;
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
