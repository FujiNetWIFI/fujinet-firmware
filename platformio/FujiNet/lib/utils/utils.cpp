#include <algorithm>

#include "utils.h"
// trim from start (in place)
void util_ltrim(std::string &s)
{
    s.erase(
        s.begin(),
        std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); })
    );
}

// trim from end (in place)
void util_rtrim(std::string &s) 
{
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

// trim from both ends (in place)
void util_trim(std::string &s) {
    util_ltrim(s);
    util_rtrim(s);
}
