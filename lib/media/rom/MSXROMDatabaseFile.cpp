#include "MSXROMDatabase.h"

#include "fnFsSD.h"

static constexpr const char *MSX_ROM_DB_PATH = "/msxromdb.txt";

FILE *msx_rom_db_open()
{
    if (!fnSDFAT.running() || !fnSDFAT.exists(MSX_ROM_DB_PATH))
        return nullptr;
    return fnSDFAT.file_open(MSX_ROM_DB_PATH);
}
