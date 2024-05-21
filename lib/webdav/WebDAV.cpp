/** 
 * WebDAV parsing class for directory output
 */

#include "WebDAV.h"

#include <cstring>

#include "../../include/debug.h"

// check if element el is matching pattern *:pat (i.e. ends with ":"+pat)
#define IS_ANYNS_ELEMENT(pat, el, el_len) (el_len >= sizeof(pat) && strcmp(el+el_len-sizeof(pat), ":" pat) == 0)


/**
     * @brief Template to wrap Start call.
     * @param data pointer to parent class
     * @param El the current element being parsed
     * @param attr the array of attributes attached to element
     */
template <class T>
void call_start(void *data, const XML_Char *El, const XML_Char **attr)
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
void call_end(void *data, const XML_Char *El)
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
void call_char(void *data, const XML_Char *s, int len)
{
    T *handler = static_cast<T *>(data);
    handler->Char(s, len);
}

bool WebDAV::begin_parser()
{
    // Create XML parser
    parser = XML_ParserCreate(NULL);
    if (parser == nullptr)
    {
        Debug_printf("WebDAV::init() - could not create expat parser.\r\n");
        return true;
    }
    // Set it up
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, call_start<WebDAV>, call_end<WebDAV>);
    XML_SetCharacterDataHandler(parser, call_char<WebDAV>);

    insideResponse = false;
    insideDisplayName = false;
    insideGetContentLength = false;
    entriesCounter = 0;

    // Clear result storage
    clear();
    return false;
}

void WebDAV::end_parser(bool clear_entries)
{
    if (parser != nullptr)
    {
        XML_ParserFree(parser);
        parser = nullptr;
    }
    if (clear_entries)
        clear();
}

bool WebDAV::parse(const char *buf, int len, int isFinal)
{
    if (parser == nullptr)
    {
        Debug_printf("WebDAV::parse() - no parser!\r\n");
        return true;
    }

    // Put PROPFIND data to debug console
    Debug_printf("WebDAV::parse data (%d bytes):\r\n", len);
    if (len > 0)
        Debug_printf("%s\r\n", buf);

    // Parse the damned buffer
    XML_Status xs = XML_Parse(parser, buf, len, isFinal);

    if (xs == XML_STATUS_ERROR)
    {
        Debug_printf("DAV response XML Parse Error! msg: %s line: %lu\r\n",
            XML_ErrorString(XML_GetErrorCode(parser)), XML_GetCurrentLineNumber(parser));
        return true;
    }
    return false;
}

void WebDAV::clear()
{
    entries.clear();
    entries.shrink_to_fit();
    currentEntry.filename.clear();
    currentEntry.fileSize.clear();
    currentEntry.isDir = false;
}

void WebDAV::Start(const XML_Char *el, const XML_Char **attr)
{
    // Debug_printf("WebDAV::Start(%s, %p)\n", el, attr);
    size_t el_len = strlen(el);
    if (IS_ANYNS_ELEMENT("response", el, el_len))
    {
        Debug_println("Response Entry:");
        insideResponse = true;
    }
    else if (IS_ANYNS_ELEMENT("displayname", el, el_len))
        insideDisplayName = true;
    else if (IS_ANYNS_ELEMENT("getcontentlength", el, el_len))
        insideGetContentLength = true;
}

void WebDAV::End(const XML_Char *el)
{
    size_t el_len = strlen(el);
    if (IS_ANYNS_ELEMENT("response", el, el_len))
    {
        insideResponse = false;

        bool store = true;
        // skip first entry (current directory)
        if (entriesCounter++ == 0) 
            store = false;
        // skip entries over limit
        else if (entriesCounter >= 1000)
        {
            store = false;
            if (entriesCounter == 1000)
                Debug_printf("Too many directory entries");
        }
        // skip noname entries
        else if (currentEntry.filename.empty())
            store = false;

        // store directory entry
        if (store)
            entries.push_back(currentEntry);

        // reset currentEntry
        currentEntry.filename.clear();
        currentEntry.fileSize.clear();
        currentEntry.isDir = false;
    }
    else if (IS_ANYNS_ELEMENT("displayname", el, el_len))
        insideDisplayName = false;
    else if (IS_ANYNS_ELEMENT("collection", el, el_len))
        currentEntry.isDir = true;
    else if (IS_ANYNS_ELEMENT("getcontentlength", el, el_len))
        insideGetContentLength = false;
}

void WebDAV::Char(const XML_Char *s, int len)
{
    if (insideResponse == true)
    {
        if (insideDisplayName == true)
        {
            currentEntry.filename = std::string(s, len);
            Debug_printf("  filename = %s\n", currentEntry.filename.c_str());
        }
        else if (insideGetContentLength == true)
        {
            currentEntry.fileSize = std::string(s, len);
            Debug_printf("  fileSize = %s\n", currentEntry.fileSize.c_str());
        }
    }
}
