#include <Arduino.h>
#include <sstream>
#include <string>

#include <WiFi.h>

#include "httpServiceParser.h"
#include "../../src/main.h"

using namespace std;

const string fnHttpServiceParser::substitute_tag(const string &tag)
{
    enum tagids {
        FN_HOSTNAME = 0,
        FN_VERSION,
        FN_IPADDRESS,
        FN_LASTTAG
    };

    const char *tagids[FN_LASTTAG] =
    {
        "FN_HOSTNAME",
        "FN_VERSION",
        "FN_IPADDRESS"
    };

    stringstream resultstream;
    #ifdef DEBUG
        Debug_printf("Substituting tag '%s'\n", tag.c_str());
    #endif

    int tagid;
    for(tagid = 0; tagid < FN_LASTTAG; tagid++)
    {
        if(0 == tag.compare(tagids[tagid]))
        {
            break;
        }
    }

    // Provide a replacement value
    switch(tagid)
    {
    case FN_HOSTNAME:
        resultstream << WiFi.getHostname();
        break;
    case FN_VERSION:
        resultstream << FUJINET_VERSION;
        break;
    case FN_IPADDRESS:
        resultstream << WiFi.localIP().toString().c_str();
        break;
    default:
        resultstream << tag;
        break;
    }
    #ifdef DEBUG
        Debug_printf("Substitution result: \"%s\"\n", resultstream.str().c_str());
    #endif
    return resultstream.str();
}

bool fnHttpServiceParser::is_parsable(const char *extension)
{
    if(extension != NULL)
    {
        if(strncmp(extension, "html", 4) == 0)
            return true;
    }
    return false;
}

/* Look for anything between <% and %> tags
 And send that to a routine that looks for suitable substitutions
 Returns string with subtitutions in place
*/
string fnHttpServiceParser::parse_contents(const string &contents)
{
    std::stringstream ss;
    uint pos = 0, x, y;
    do {
        x = contents.find("<%", pos);
        if( x == string::npos) {
            ss << contents.substr(pos);
            break;
        }
        // Found opening tag, now find ending
        y = contents.find("%>", x+2);
        if( y == string::npos) {
            ss << contents.substr(pos);
            break;
        }
        // Now we have starting and ending tags
        if( x > 0)
            ss << contents.substr(pos, x-pos);
        ss << substitute_tag(contents.substr(x+2, y-x-2));
        pos = y+2;
    } while(true);

    return ss.str();
}
