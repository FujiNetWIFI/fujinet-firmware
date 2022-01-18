#include "fuji.h"

iwmFuji theFuji; // Global fuji object.

void iwmFuji::shutdown()
{
}

iwmDisk *iwmFuji::bootdisk()
{
    return nullptr;
}

void iwmFuji::insert_boot_device(uint8_t d)
{
}

void iwmFuji::setup(iwmBus *iwmbus)
{
}

void iwmFuji::image_rotate()
{
}
int iwmFuji::get_disk_id(int drive_slot)
{
    return -1;
}
std::string iwmFuji::get_host_prefix(int host_slot)
{
    return std::string();
}

void iwmFuji::_populate_slots_from_config()
{
}
void iwmFuji::_populate_config_from_slots()
{
}

void iwmFuji::apple_mount_all() // 0xD7
{
}

iwmFuji::iwmFuji()
{
}
