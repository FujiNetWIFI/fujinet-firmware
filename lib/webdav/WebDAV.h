/** 
 * WebDAV parsing class for directory output
 */

#ifndef WebDAV_H
#define WebDAV_H

#include <expat.h>
#include <string>
#include <vector>

using namespace std;

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
        string filename;
        /**
         * Entry filesize
         */
        size_t filesize;
    };

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
    vector<DAVEntry> entries;

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
};

/**
 * @brief Template to wrap Start call.
 * @param data pointer to parent class
 * @param El the current element being parsed
 * @param attr the array of attributes attached to element
 */
template <class T>
void Start(void *data, const XML_Char *El, const XML_Char **attr)
{
    T *handler = static_cast<T *>(data);
    handler->Start(El, attr);
}

/**
 * @brief Template to wrap End call
 * @param data pointer to parent class.
 * @param El the current element being parsed.
 **/
template <class T>
void End(void *data, const XML_Char *El)
{
    T *handler = static_cast<T *>(data);
    handler->End(El);
}

/**
 * @brief template to wrap character data.
 * @param data pointer to parent class
 * @param s pointer to the character data
 * @param len length of character data at pointer
 **/
template <class T>
void Char(void *data, const XML_Char *s, int len)
{
    T *handler = static_cast<T *>(data);
    handler->Char(s, len);
}

#endif /* WebDAV_H */