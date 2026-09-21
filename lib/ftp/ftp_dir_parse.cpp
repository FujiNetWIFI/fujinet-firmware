/**
 * ftp_dir_parse implementation
 */

#include "ftp_dir_parse.h"

#include "ftpparse.h"

fujiError_t ftp_next_dir_entry(std::istream &listing, std::string &name,
                               long &filesize, bool &is_dir)
{
    std::string line;
    struct ftpparse parse;

    while (getline(listing, line))
    {
        // Strip trailing \r if present (CRLF normalization)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
            continue;

        if (ftpparse(&parse, (char *)line.c_str(), line.length()))
        {
            name = std::string(parse.name ? parse.name : "???");

            // Strip symlink target from name (e.g., "transfer -> crossplatform/transfer/" becomes "transfer")
            size_t arrow_pos = name.find(" -> ");
            if (arrow_pos != std::string::npos)
            {
                name = name.substr(0, arrow_pos);
            }

            filesize = parse.size;
            is_dir = (parse.flagtrycwd == 1);
            return FUJI_ERROR::NONE; // Successfully parsed one entry
        }
    }

    return FUJI_ERROR::UNSPECIFIED; // End of directory (EOF)
}
