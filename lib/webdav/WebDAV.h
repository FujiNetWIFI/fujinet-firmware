/** 
 * WebDAV parsing class for directory output
 */

#ifndef WebDAV_H
#define WebDAV_H

#include <expat.h>
#include <string>
#include <vector>

// using namespace std;

/**
 * @brief a class wrapping expat parser for directory entries
 */
class WebDAV
{
public:
    /**
     * @brief container class for one WebDAV directory entry
     */
    class DAVEntry
    {
    public:
        /**
         * Entry filename
         */
        std::string filename;
        /**
         * Directory flag
         */
        bool isDir;
        /**
         * Entry filesize
         */
        std::string fileSize;
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
    std::vector<WebDAV::DAVEntry>::iterator rewind() {return entries.begin();};

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
    std::vector<DAVEntry> entries;

protected:
    /**
     * @brief the current entry
     */
    DAVEntry currentEntry;

    /**
     * Are we inside D:response?
     */
    bool insideResponse;

    /**
     * Are we inside D:displayname?
     */
    bool insideDisplayName;

    /**
     * Are we inside D:getcontentlength?
     */
    bool insideGetContentLength;

    /**
     * Expat XML parser
     */
    XML_Parser parser;

    /*
     * Parsed entries counter
     */
    int entriesCounter;
};

#endif /* WebDAV_H */