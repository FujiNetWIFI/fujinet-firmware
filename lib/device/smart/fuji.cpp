#include "fuji.h"

appleFuji theFuji; // Global fuji object.

void appleFuji::shutdown()
{
}

smartDisk *appleFuji::bootdisk()
{
    return nullptr;
}

void appleFuji::insert_boot_device(uint8_t d)
{
}

void appleFuji::setup(smartBus *smartbus)
{
}

void appleFuji::image_rotate()
{
}
int appleFuji::get_disk_id(int drive_slot)
{
    return -1;
}
std::string appleFuji::get_host_prefix(int host_slot)
{
    return std::string();
}

void appleFuji::_populate_slots_from_config()
{
}
void appleFuji::_populate_config_from_slots()
{
}

void appleFuji::apple_mount_all() // 0xD7
{
}

appleFuji::appleFuji()
{
}
