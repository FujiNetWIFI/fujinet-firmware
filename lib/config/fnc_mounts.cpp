#include "fnConfig.h"
#include <cstring>

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
