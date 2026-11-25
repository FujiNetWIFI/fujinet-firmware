#ifndef MSXROMDATABASE_H
#define MSXROMDATABASE_H

#include <cstdio>
#include <optional>
#include <string>

// Opens the ROM database on the SD card; nullptr if there is none. The caller
// closes it with fclose().
FILE *msx_rom_db_open();

// Scans db, whose lines are "SHA1<TAB>TypeName", for sha1hex (case-insensitive)
// and returns that line's type name.
std::optional<std::string> msx_rom_db_lookup(FILE *db, const std::string &sha1hex);

#endif /* MSXROMDATABASE_H */
