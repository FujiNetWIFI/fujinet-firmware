#include <Arduino.h>
#include <sstream>
#include "httpServiceParser.h"

using namespace std;

string fnHttpServiceParser::substitute_tag(const string &tag)
{
    string result(tag);
    #ifdef DEBUG
        Debug_printf("Substituting tag '%s'\n", result.c_str());
    #endif
    return result;
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
    #ifdef DEBUG
        Debug_println("Starting content parsing");
    #endif
    std::stringstream ss;
    uint pos = 0, x, y;
    do {
        x = contents.find("<%", pos);
        if( x == string::npos) {
            ss << contents.substr(pos);
            break;
        }
        // Found opening tag, now find ending
        y = contents.find("%>", pos+x+2);
        if( y == string::npos) {
            ss << contents.substr(pos);
            break;
        }
        // Now we have starting and ending tags
        if( x > 0)
            ss << contents.substr(pos, x);
        ss << substitute_tag(contents.substr(pos+x+2, y-x-2));
        pos += y+2;
    } while(true);

    #ifdef DEBUG
        Debug_println("Finished parsing");
    #endif
    return ss.str();
}
