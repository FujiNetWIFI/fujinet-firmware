/** 
 * WebDAV parsing class for directory output
 */

#include "WebDAV.h"

#include <cstring>

#include "../../include/debug.h"

// check if element el is matching pattern *:pat (i.e. ends with ":"+pat)
#define IS_ANYNS_ELEMENT(pat, el, el_len) (el_len >= sizeof(pat) && strcmp(el+el_len-sizeof(pat), ":" pat) == 0)

void WebDAV::Start(const XML_Char *el, const XML_Char **attr)
{
    Debug_printf("WebDAV::Start(%s, )\n", el);
    size_t el_len = strlen(el);
    if (IS_ANYNS_ELEMENT("response", el, el_len))
    {
        insideResponse = true;
         // reset entry name and size
        currentEntry.filename.clear();
        currentEntry.fileSize.clear();
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
        entries.push_back(currentEntry);
    }
    else if (IS_ANYNS_ELEMENT("displayname", el, el_len))
        insideDisplayName = false;
    else if (IS_ANYNS_ELEMENT("getcontentlength", el, el_len))
        insideGetContentLength = false;
}

void WebDAV::Char(const XML_Char *s, int len)
{
    if (insideResponse == true)
    {
        if (insideDisplayName == true)
            currentEntry.filename = std::string(s, len);
        else if (insideGetContentLength == true)
            currentEntry.fileSize = std::string(s, len);
    }
}