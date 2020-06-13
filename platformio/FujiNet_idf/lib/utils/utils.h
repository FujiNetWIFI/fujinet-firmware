#ifndef _FN_UTILS_H
#define _FN_UTILS_H

#include <string>

#define __BEGIN_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic push")    \
    _Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"") \
        _Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
#define __END_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic pop")

#define __IGNORE_UNUSED_VAR(v) (void)v
#define __IGNORE_UNSUED_PVAR(v) (void*)v

void util_ltrim(std::string &s);
void util_rtrim(std::string &s);
void util_trim(std::string &s);
long util_parseInt(FILE *f, char skipChar);
long util_parseInt(FILE *f);
unsigned char util_checksum(const char *chunk, int length);
std::string util_crunch(std::string filename);
std::string util_entry(std::string crunched, size_t fileSize);
std::string util_long_entry(std::string filename, size_t fileSize);
std::string util_ellipsize(std::string longString, int maxLength);
bool util_wildcard_match(char str[], char pattern[], int n, int m);

#endif // _FN_UTILS_H