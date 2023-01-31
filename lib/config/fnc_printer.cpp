#include "fnConfig.h"
#include <cstring>
#include "../../include/debug.h"

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
