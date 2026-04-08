#include <cstdint>
#include <fstream>
#include <string>

#include "debug.h"
#include "fnFsSD.h"
#include "rs232/diskTypeImg.h"
#include "string_utils.h"
#include "romType.h"

#include "romIdentifier.h"

std::map<std::string, fujiROMType_t> name_to_type = {
	{"ASCII8", ROM_TYPE_MSX_ASCII8},
	{"ASCII16", ROM_TYPE_MSX_ASCII16},
	{"Konami", ROM_TYPE_MSX_KONAMI},
	{"KonamiSCC", ROM_TYPE_MSX_KONAMI_SCC},
};

RomIdentifier::RomIdentifier(void)
{
	Debug_printf("RomIdentifier::RomIdentifier\n");
	// TODO: Read from config to allow override
	db_filepath = "/msxromdb.txt";
	load_db();
}

RomIdentifier::~RomIdentifier(void)
{
	return;
}

bool RomIdentifier::load_db()
{
	FILE *fin = NULL; //declare fin
    if (fnSDFAT.running() && fnSDFAT.exists(db_filepath.c_str()))
    {
        Debug_printf("Loading ROM database (%s) from SD\n", db_filepath.c_str());
        fin = fnSDFAT.file_open(db_filepath.c_str());
    }

    if (fin == nullptr)
    {
    	Debug_println("Failed to load ROM database");
     	return false;
    }

    char line[64];
    while (fgets(line, 64, fin))
    {
       	std::string row = std::string(line);
        if (row.size() < 40)
        	continue;
       	size_t split = row.find("\t");
       	std::string sha1 = row.substr(0, split);
       	std::string type_str = row.substr(split + 1, row.size() - split - 2);
       	sha1_to_type[sha1] = get_type_from_name(type_str);
    }

    return true;
}

fujiROMType_t RomIdentifier::get_type_from_name(std::string name) {
    auto it = name_to_type.find(name);
    if (it != name_to_type.end()) {
	    return it->second;
    }
   	return ROM_TYPE_UNKNOWN;
}

fujiROMType_t RomIdentifier::get_type_for_sha1(std::string sha1) {
	auto it = sha1_to_type.find(sha1);
    if (it != sha1_to_type.end()) {
	    return it->second;
    }
   	return ROM_TYPE_UNKNOWN;
}

fujiROMType_t RomIdentifier::identify_rom_type(std::vector<uint8_t> rom_data)
{
	std::string rom_str(rom_data.begin(), rom_data.end());
	std::string sha1 = mstr::sha1(rom_str);
	Debug_printf("RomIdentifier sha1: %s\n", sha1.c_str());
	if (sha1 == "") {
		return ROM_TYPE_UNKNOWN;
	}
	fujiROMType_t t = get_type_for_sha1(sha1);
	Debug_printf("RomIdentifier fujiROMType t: %u\n", t);
	return t;
}
