/** 
 * Index HTML parsing class for directory output
 */

#include "IndexParser.h"

#include <cstring>
#include <iostream>
#include <sstream>

#include "../../include/debug.h"

#ifdef ESP_PLATFORM
#define MAX_DIR_ENTRIES 1000
#else
#define MAX_DIR_ENTRIES 5000
#endif

// // check if element el is matching pattern *:pat (i.e. ends with ":"+pat)
// #define IS_ANYNS_ELEMENT(pat, el, el_len) (el_len >= sizeof(pat) && strcmp(el+el_len-sizeof(pat), ":" pat) == 0)

// check if element el is pat
#define IS_ELEMENT(pat, el, el_len) (el_len == sizeof(pat)-1 && strcmp(el, pat) == 0)


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

bool IndexParser::begin_parser()
{
    // Create XML parser
    parser = XML_ParserCreate(NULL);
    if (parser == nullptr)
    {
        Debug_printf("IndexParser::init() - could not create expat parser.\r\n");
        return true;
    }
    // Set it up
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, call_start<IndexParser>, call_end<IndexParser>);
    XML_SetCharacterDataHandler(parser, call_char<IndexParser>);

    collectingText = false;
    // insideResponse = false;
    // insideDisplayName = false;
    // insideGetContentLength = false;
    entriesCounter = 0;

    // Clear result storage
    clear();
    return false;
}

void IndexParser::end_parser(bool clear_entries)
{
    if (parser != nullptr)
    {
        XML_ParserFree(parser);
        parser = nullptr;
    }
    if (clear_entries)
        clear();
}

bool IndexParser::parse(const char *buf, int len, int isFinal)
{
    if (parser == nullptr)
    {
        Debug_printf("IndexParser::parse() - no parser!\r\n");
        return true;
    }

    // Debug_printf("IndexParser::parse data (%d bytes):\r\n", len);
    // if (len > 0)
    //     Debug_printf("%.*s\r\n", len, buf);

    // Parse the damned buffer
    XML_Status xs = XML_Parse(parser, buf, len, isFinal);

    if (xs == XML_STATUS_ERROR)
    {
        Debug_printf("Index Parse Error! %d msg: %s line: %lu\r\n",
            XML_GetErrorCode(parser), XML_ErrorString(XML_GetErrorCode(parser)), XML_GetCurrentLineNumber(parser));
        // Ignore errors if we parsed some data and return what we have
        if (entriesCounter > 3 || (XML_GetErrorCode(parser) == XML_ERROR_TAG_MISMATCH && entriesCounter > 0))
            return false;
        return true;
    }
    return false;
}

void IndexParser::clear()
{
    entries.clear();
    entries.shrink_to_fit();
    currentEntry.filename.clear();
    currentEntry.fileSize.clear();
    currentEntry.mTime.clear();
    currentEntry.isDir = false;
    entryText.clear();
}

void IndexParser::Start(const XML_Char *el, const XML_Char **attr)
{
    // Debug_printf("IndexParser::Start(%s)\n", el);
    // for (int i = 0; attr[i]; i += 2)
    // {
    //     Debug_printf("  %s=%s\n", attr[i], attr[i + 1]);
    // }

    if (collectingText)
    {
        collectingText = false;

        // New tag started, store previous entry

        // get date, time and size from entry text
        //Debug_printf("entry text: %s\n", entryText.c_str());
        std::istringstream stream(entryText);
        std::string token;
        std::vector<std::string> tokens;
        while (stream >> token) {
            //Debug_printf("  %s\n", token.c_str());
            tokens.push_back(token);
        }
        if (tokens.size() == 3)
        {
            // Debug_printf("  mtime: %s %s size: %s\n", tokens[0].c_str(), tokens[1].c_str(), tokens[2].c_str());
            currentEntry.mTime = tokens[0] + " " + tokens[1];
            currentEntry.fileSize = tokens[2];
        }

        bool store = true;
        entriesCounter++;
        // skip entries over limit
        if (entriesCounter > MAX_DIR_ENTRIES)
        {
            store = false;
            if (entriesCounter == MAX_DIR_ENTRIES+1)
                Debug_println("Too many directory entries");
        }
        // skip noname entries
        else if (currentEntry.filename.empty())
            store = false;
        // skip hidden entries
        else if (currentEntry.filename[0] == '.')
            store = false;

        // store directory entry
        if (store)
            entries.push_back(currentEntry);

        // reset currentEntry
        currentEntry.filename.clear();
        currentEntry.fileSize.clear();
        currentEntry.mTime.clear();
        currentEntry.isDir = false;
        entryText.clear();
    }

    size_t el_len = strlen(el);
    if (IS_ELEMENT("a", el, el_len))
    {
        for (int i = 0; attr[i]; i += 2) 
        {
            if (strcmp(attr[i], "href") == 0)
            {
                const char *name = attr[i+1];
                int name_len = strlen(name);
                if (name_len > 0 && name[name_len-1] == '/')
                {
                    currentEntry.isDir = true;
                    currentEntry.filename = std::string(name, name_len-1);
                }
                else
                {
                    currentEntry.isDir = false;
                    currentEntry.filename = std::string(name, name_len);
                }
            }
        }
    }
}

void IndexParser::End(const XML_Char *el)
{
    // Debug_printf("IndexParser::End(%s)\n", el);

    size_t el_len = strlen(el);
    if (IS_ELEMENT("a", el, el_len))
    {
        collectingText = true;
    }
}

void IndexParser::Char(const XML_Char *s, int len)
{
    // Debug_printf("IndexParser::Char(%.*s)\n", len, s);

    if (collectingText)
    {
        entryText += std::string(s, len);
    }
}
