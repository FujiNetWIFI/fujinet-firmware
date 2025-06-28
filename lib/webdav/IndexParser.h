/** 
 * Index HTML parsing class for directory output
 */

#ifndef INDEXPARSER_H
#define INDEXPARSER_H

#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "../../include/PSRAMAllocator.h"
#endif

// using namespace std;

/**
 * @brief a class wrapping expat parser for directory entries
 */
class IndexParser
{
public:
    /**
     * @brief container class for one IndexParser directory entry
     */
    class IndexEntry
    {
    public:
        // Entry filename
        std::string filename;
        // Directory flag
        bool isDir;
        // Entry filesize
        std::string fileSize;
        // Entry modified time
        std::string mTime;
    };

    /**
     * @brief Called to setup everything before processing XML
     */
    bool begin_parser();

    /**
     * @brief Called to release XML parser resources
     * @param clear_entries call clear() too
     */
    void end_parser(bool clear_entries = false);

    /**
     * @brief Called to parse data chunk
     */
    bool parse(const char *buf, int len, int isFinal);

	bool parse_line(std::string &line);

	/**
     * @brief Called to scoot to beginning of directory entries
     */
#ifdef ESP_PLATFORM
    std::vector<IndexEntry,PSRAMAllocator<IndexEntry>>::iterator rewind() {return entries.begin();};
#else
    std::vector<IndexEntry>::iterator rewind() {return entries.begin();};
#endif

    /**
     * @brief Called to remove all stored directory entries
     */
    void clear();

    /**
     * @brief collection of directory entries.
     */
#ifdef ESP_PLATFORM
    std::vector<IndexEntry,PSRAMAllocator<IndexEntry>> entries;
#else
    std::vector<IndexEntry> entries;
#endif


protected:
    /**
     * @brief the current entry
     */
    IndexEntry currentEntry;

    bool isIndexOf;
    std::string lineBuffer;

    /*
     * Parsed entries counter
     */
    int entriesCounter;
};

#endif /* INDEXPARSER_H */