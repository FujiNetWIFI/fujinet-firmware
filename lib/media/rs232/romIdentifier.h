#ifndef ROM_IDENTIFIER_H
#define ROM_IDENTIFIER_H

#include <map>
#include <string>

#include "romType.h"
#include "diskTypeImg.h"

#define ROM_DB_FILENAME "/msxromdb.txt"

class RomIdentifier
{
private:
	std::map<std::string, fujiROMType_t> sha1_to_type;
	std::string db_filepath;
	bool load_db();

public:
	RomIdentifier();
	~RomIdentifier();
	fujiROMType_t get_type_for_sha1(std::string sha1);
	fujiROMType_t get_type_from_name(std::string name);
	fujiROMType_t identify_rom_type(std::vector<uint8_t> rom_data);

};

#endif // ROM_INDENTFIER_H
