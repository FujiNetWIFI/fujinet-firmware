#include "fnConfig.h"
#include <cstring>

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
