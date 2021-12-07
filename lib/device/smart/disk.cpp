#include "disk.h"

void smartDisk::smart_read()
{
}

void smartDisk::smart_write(bool verify)
{
}
// void smart_format();
void smartDisk::smart_status() // override;
{
}
void smartDisk::smart_process() // uint32_t commanddata, uint8_t checksum); // override;
{
}
// void derive_percom_block(uint16_t numSectors);
// void smart_read_percom_block();
// void smart_write_percom_block();
// void dump_percom_block();

void smartDisk::shutdown()
{
}

smartDisk::smartDisk()
{
}

mediatype_t smartDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    return disk_type;
}

void smartDisk::unmount()
{
}

bool smartDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    return false;
}
