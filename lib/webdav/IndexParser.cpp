/** 
 * Index HTML parsing class for directory output
 */

#include "IndexParser.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include "string_utils.h"

#include "../../include/debug.h"

#ifdef ESP_PLATFORM
#define MAX_DIR_ENTRIES 1000
#else
#define MAX_DIR_ENTRIES 5000
#endif


bool IndexParser::begin_parser()
{
    isIndexOf = false;
    entriesCounter = 0;

    // Clear result storage
    clear();
    return false;
}

void IndexParser::end_parser(bool clear_entries)
{
    if (clear_entries)
        clear();
}

bool IndexParser::parse(const char *buf, int len, int isFinal)
{
    // Debug_printf("IndexParser::parse data (%d bytes):\r\n", len);
    // if (len > 0)
    //     Debug_printf("%.*s\r\n", len, buf);

    // Append input to line buffer
    if (buf != nullptr && len > 0)
        lineBuffer += std::string(buf, len);

    // Process lines
    size_t pos = lineBuffer.find('\n');
    if (isFinal && pos == std::string::npos)
    {
        // ensure last line is processed, even if no newline
        pos = lineBuffer.size();
        isFinal = false;
    }
    while (pos != std::string::npos)
    {
        std::string line = lineBuffer.substr(0, pos);
        lineBuffer.erase(0, pos + 1);
        // Debug_printf("Line: %s\n", line.c_str());

        if (!isIndexOf)
        {
            // Check for "Index of /..." in title
            size_t index_pos = line.find("<title>Index of /");
            if (index_pos != std::string::npos)
                isIndexOf = true;
        }
        else 
        {
            // Extract directory entry, if any
            if (entriesCounter < MAX_DIR_ENTRIES && parse_line(line))
            {
                // store entry
                entries.push_back(currentEntry);
                // reset currentEntry
                currentEntry.filename.clear();
                currentEntry.fileSize.clear();
                currentEntry.mTime.clear();
                currentEntry.isDir = false;
                if (++entriesCounter == MAX_DIR_ENTRIES)
                    Debug_println("Too many directory entries");
            }
        }
        // Next line
        pos = lineBuffer.find('\n');
        if (isFinal && pos == std::string::npos)
        {
            // ensure last line is processed, even if no newline
            pos = lineBuffer.size();
            isFinal = false;
        }
    }

    return false;
}

bool IndexParser::parse_line(std::string &line)
{
    bool match = false;

    std::string lline = line;
    mstr::toLower(lline);

    // Check for (last) href on line
    size_t pos = lline.rfind("<a href=\"");
    if (pos == std::string::npos)
        return false;

    // Extract href link
    pos += 9; // Move past '<a href="'
    size_t end_pos = lline.find("\"", pos);
    if (end_pos != std::string::npos) 
    {
        std::string link = line.substr(pos, end_pos - pos);
        // Debug_printf("Link: %s\n", link.c_str());
        // Skip blank, hidden, absolute, query and section links
        if (link.size() > 0  && link[0] != '.' && link[0] != '/' && link[0] != '#' && link[0] != '?')
        {
            match = true;
            if (link[link.size()-1] == '/')
            {
                currentEntry.isDir = true;
                currentEntry.filename = link.substr(0, link.size()-1);
            }
            else
            {
                currentEntry.isDir = false;
                currentEntry.filename = link;
            }
            // Extract date time and size - Apache mod_dir format
            //   <tr><td valign="top"><img src="/icons/unknown.gif" alt="[   ]"></td><td><a href="_lobby.xex">_lobby.xex</a></td><td align="right">2025-02-23 15:33  </td><td align="right">7.4K</td><td>&nbsp;</td></tr>
            pos = lline.find("<td align=\"right\">", end_pos);
            if (pos != std::string::npos) 
            {
                pos += 18; // Move past '<td align="right">'
                end_pos = lline.find("</td>", pos);
                if (end_pos != std::string::npos) 
                {
                    std::string date_time = line.substr(pos, end_pos - pos);
                    currentEntry.mTime = date_time;
                    // Debug_printf("Extracted date and time: %s\n", date_time.c_str());
                }    
                // Extract size, Apache format
                pos = lline.find("<td align=\"right\">", end_pos);
                if (pos != std::string::npos) 
                {
                    pos += 18; // Move past '<td align="right">'
                    end_pos = lline.find("</td>", pos);
                    if (end_pos != std::string::npos) 
                    {
                        std::string file_size = line.substr(pos, end_pos - pos);
                        currentEntry.fileSize = file_size;
                        // Debug_printf("Extracted size: %s\n", file_size.c_str());
                    }
                }
            }
            else
            {
                // Extract date time and size - Nginx format
                //   <a href="plato.tap">plato.tap</a>               19-Mar-2024 17:33               26841
                pos = lline.find("</a>", end_pos);
                if (pos != std::string::npos) 
                {
                    pos += 4; // Move past '</a>'
                    std::istringstream stream(line.substr(pos));
                    std::string token;
                    std::vector<std::string> tokens;
                    while (stream >> token) tokens.push_back(token);
                    if (tokens.size() == 3)
                    {
                        // Debug_printf("  mtime: %s %s size: %s\n", tokens[0].c_str(), tokens[1].c_str(), tokens[2].c_str());
                        currentEntry.mTime = tokens[0] + " " + tokens[1];
                        currentEntry.fileSize = tokens[2];
                    }
            
                }
            }
            // Skip entries without mTime and fileSize
            if (currentEntry.mTime.empty() && currentEntry.fileSize.empty())
            {
                currentEntry.filename.clear();
                currentEntry.fileSize.clear();
                currentEntry.mTime.clear();
                match = false;
            }
        }
    }
    return match;
}


void IndexParser::clear()
{
    entries.clear();
    entries.shrink_to_fit();
    currentEntry.filename.clear();
    currentEntry.filename.shrink_to_fit();
    currentEntry.fileSize.clear();
    currentEntry.fileSize.shrink_to_fit();
    currentEntry.mTime.clear();
    currentEntry.mTime.shrink_to_fit();
    lineBuffer.clear();
    lineBuffer.shrink_to_fit();
    currentEntry.isDir = false;
}
