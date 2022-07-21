#ifndef _FN_STRUTILS_H
#define _FN_STRUTILS_H

#include <string>
#include <vector>
#include <cstring>

//#include "../../include/global_defines.h"

namespace mstr {
    std::string drop(std::string str, size_t count);
    std::string dropLast(std::string str, size_t count);
    bool startsWith(std::string s, const char *pattern, bool case_sensitive = true);
    bool endsWith(std::string s, const char *pattern, bool case_sensitive = true);
    bool equals(std::string &s1, std::string &s2, bool case_sensitive = true);
    bool equals(std::string &s1, char* s2, bool case_sensitive = true);
    bool contains(std::string &s1, char *s2, bool case_sensitive = true);
    std::vector<std::string> split(std::string toSplit, char ch, int limit = 9999);
    void toLower(std::string &s);
    void toUpper(std::string &s);
    void ltrim(std::string &s);
    void rtrim(std::string &s);
    void rtrimA0(std::string &s);
    void trim(std::string &s);
    void replaceAll(std::string &s, const std::string &search, const std::string &replace);
    std::string joinToString(std::vector<std::string>::iterator* start, std::vector<std::string>::iterator* end, std::string separator);
    std::string joinToString(std::vector<std::string>, std::string separator);
    std::string urlEncode(std::string s);
    std::string urlDecode(std::string s);
    void toASCII(std::string &s);
    void toPETSCII(std::string &s);
    bool isText(std::string &s);
    bool isA0Space(int ch);
    void A02Space(std::string &s);
    std::string format(const char *format, ...);
}

#endif