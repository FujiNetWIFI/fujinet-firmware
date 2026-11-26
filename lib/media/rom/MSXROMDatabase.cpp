#include "MSXROMDatabase.h"

#include <cctype>
#include <cstring>

static constexpr size_t SHA1_HEX_LENGTH = 40;

static bool hash_matches(const char *line, const std::string &sha1hex)
{
    if (sha1hex.size() != SHA1_HEX_LENGTH)
        return false;
    for (size_t idx = 0; idx < SHA1_HEX_LENGTH; idx++)
    {
        if (std::toupper(static_cast<unsigned char>(line[idx]))
            != std::toupper(static_cast<unsigned char>(sha1hex[idx])))
            return false;
    }
    return true;
}

std::optional<std::string> msx_rom_db_lookup(FILE *db, const std::string &sha1hex)
{
    if (db == nullptr)
        return std::nullopt;

    char line[128];
    bool continued = false; // the rest of an overlong line is being skipped
    while (fgets(line, sizeof(line), db) != nullptr)
    {
        size_t len = strlen(line);
        bool truncated = len == sizeof(line) - 1 && line[len - 1] != '\n';
        bool discard = continued || truncated;
        continued = truncated;
        if (discard)
            continue;

        while (len > 0 && std::isspace(static_cast<unsigned char>(line[len - 1])))
            line[--len] = '\0';
        if (len <= SHA1_HEX_LENGTH || line[SHA1_HEX_LENGTH] != '\t')
            continue;
        if (!hash_matches(line, sha1hex))
            continue;

        std::string type(line + SHA1_HEX_LENGTH + 1);
        if (!type.empty())
            return type;
    }
    return std::nullopt;
}
