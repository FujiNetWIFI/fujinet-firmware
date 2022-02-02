/* FujiNet web server helper class

Broke out parsing functions to make things easier to read.

If a file has an extention pre-determined to support parsing (see/update
    fnHttpServiceParser::is_parsable() for a the list) then the
    following happens:

    * The entire file contents are loaded into an in-memory string.
    * Anything with the pattern <%PARSE_TAG%> is replaced with an
    * appropriate value as determined by the 
    *       string substitute_tag(const string &tag)
    * function.
    * 
See const fnHttpServiceParser::substitute_tag() for
currently supported tags.

*/
#ifndef HTTPSERVICEPARSER_H
#define HTTPSERVICEPARSER_H

#include <string>

class fnHttpServiceParser
{
    static std::string format_uptime();
    static long uptime_seconds();
    static const std::string substitute_tag(const std::string &tag);
public:
    static std::string parse_contents(const std::string &contents);
    static bool is_parsable(const char *extension);
};

#endif // HTTPSERVICEPARSER_H
