/** 
 * WebDAV parsing class for directory output
 */

#include <cstring>
#include <cstdlib>
#include "WebDAV.h"

void WebDAV::Start(const XML_Char *el, const XML_Char **attr)
{
    if (strcmp(el, "D:response") == 0)
        insideResponse = true;
    else if (strcmp(el, "D:displayname") == 0)
        insideDisplayName = true;
    else if (strcmp(el, "D:getcontentlength") == 0)
        insideGetContentLength = true;
}

void WebDAV::End(const XML_Char *el)
{
    if (strcmp(el, "D:response") == 0)
    {
        insideResponse = false;
        entries.push_back(currentEntry);
    }
    else if (strcmp(el, "D:displayname") == 0)
        insideDisplayName = false;
    else if (strcmp(el, "D:getcontentlength"))
        insideGetContentLength = false;
}

void WebDAV::Char(const XML_Char *s, int len)
{
    if (insideResponse == true)
    {
        if (insideDisplayName == true)
            currentEntry.filename = string(s, len);
        else if (insideGetContentLength == true)
            currentEntry.filesize = atoi(s);
    }
}