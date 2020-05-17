#ifndef _FN_UTILS_H
#define _FN_UTILS_H

#include <string>

void util_ltrim(std::string &s);
void util_rtrim(std::string &s); 
void util_trim(std::string &s);
long util_parseInt(FILE *f, char skipChar);
long util_parseInt(FILE *f);

#endif // _FN_UTILS_H