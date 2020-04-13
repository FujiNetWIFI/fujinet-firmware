#ifndef HTTPSERVICEPARSER_H
#define HTTPSERVICEPARSER_H

class fnHttpServiceParser
{
public:
    static std::string substitute_tag(const std::string &tag);
    static std::string parse_contents(const std::string &contents);
    static bool is_parsable(const char *extension);
};

#endif // HTTPSERVICEPARSER_H
