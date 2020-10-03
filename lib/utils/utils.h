#ifndef _FN_UTILS_H
#define _FN_UTILS_H

#include <string>
#include <vector>

#define __BEGIN_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic push")    \
    _Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"") \
        _Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
#define __END_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic pop")

#define __BEGIN_IGNORE_TYPELIMITS _Pragma("GCC diagnostic push")    \
    _Pragma("GCC diagnostic ignored \"-Wtype-limits\"")
#define __END_IGNORE_TYPELIMITS _Pragma("GCC diagnostic pop")

#define __IGNORE_UNUSED_VAR(v) (void)v
#define __IGNORE_UNSUED_PVAR(v) (void*)v


// Retruns a uint16 value given two bytes in high-low order
#define UINT16_FROM_HILOBYTES(high, low) ((uint16_t)high << 8 | low)

// Returns a uint16 value from the little-endian version
#define UINT16_FROM_LE_UINT16(_ui16) \
    (_ui16 << 8 | _ui16 >> 8)
// Returns a uint32 value from the little-endian version
#define UINT32_FROM_LE_UINT32(_ui32) \
    ((_ui32 >> 24 & 0x000000FF) | (_ui32 >> 8 & 0x0000FF00) | (_ui32 << 8 & 0x00FF0000) | (_ui32 << 24 & 0xFF000000))

// Returns the high byte (MSB) of a uint16 value
#define HIBYTE_FROM_UINT16(value) ((uint8_t)((value >> 8) & 0xFF))
// Returns the low byte (LSB) of a uint16 value
#define LOBYTE_FROM_UINT16(value) ((uint8_t)(value & 0xFF))

void util_string_ltrim(std::string &s);
void util_string_rtrim(std::string &s);
void util_string_trim(std::string &s);

void util_string_tolower(std::string &s);
void util_string_toupper(std::string &s);

long util_parseInt(FILE *f, char skipChar);
long util_parseInt(FILE *f);

unsigned char util_checksum(const char *chunk, int length);
std::string util_crunch(std::string filename);
std::string util_entry(std::string crunched, size_t fileSize);
std::string util_long_entry(std::string filename, size_t fileSize);
int util_ellipsize(const char* src, char *dst, int dstsize);
//std::string util_ellipsize(std::string longString, int maxLength);
bool util_wildcard_match(const char *str, const char *pattern);

bool util_concat_paths(char *dest, const char *parent, const char *child, int dest_size);

void util_dump_bytes(uint8_t *buff, uint32_t buff_size);

std::vector<std::string> util_tokenize(std::string s, char c = ' ');
std::string util_remove_spaces(const std::string &s);

void util_strip_nonascii(std::string &s);

void util_sam_say(const char *p);

#endif // _FN_UTILS_H
