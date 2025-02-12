/** 
 * Index HTML parsing class for directory output
 */

#ifndef INDEXPARSER_H
#define INDEXPARSER_H

#include <expat.h>
#include <string>
#include <vector>

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

    /**
     * @brief Called to scoot to beginning of directory entries
     */
    std::vector<IndexParser::IndexEntry>::iterator rewind() {return entries.begin();};

    /**
     * @brief Called to remove all stored directory entries
     */
    void clear();

    /**
     * @brief Called when start tag is encountered.
     * @param el element to be processed
     * @param attr array of attributes attached to element
     */
    void Start(const XML_Char *el, const XML_Char **attr);

    /**
     * @brief called when end tag is encountered
     * @param el element to be processed
     */
    void End(const XML_Char *el);

    /**
     * @brief called when character data needs to be processed.
     * @param s pointer to character data
     * @param len length of character data
     */
    void Char(const XML_Char *s, int len);

    /**
     * @brief collection of DAV entries.
     */
    std::vector<IndexEntry> entries;

protected:
    /**
     * @brief the current entry
     */
    IndexEntry currentEntry;

    /**
     * Are we collecting text between elements?
     */
    bool collectingText;
    std::string entryText;

    // /**
    //  * Are we inside D:response?
    //  */
    // bool insideResponse;

    // /**
    //  * Are we inside D:displayname?
    //  */
    // bool insideDisplayName;

    // /**
    //  * Are we inside D:getcontentlength?
    //  */
    // bool insideGetContentLength;

    /**
     * Expat XML parser
     */
    XML_Parser parser;

    /*
     * Parsed entries counter
     */
    int entriesCounter;
};

#endif /* INDEXPARSER_H */