#ifndef _FN_UTILS_H
#define _FN_UTILS_H

#include <map>
#include <string>
#include <vector>
#include <cstdint>


#if defined(__clang__) && (__clang_major__ < 14)
#define __BEGIN_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic push")    \
    _Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
#define __END_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic pop")
#else
#define __BEGIN_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic push")    \
    _Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"") \
        _Pragma("GCC diagnostic ignored \"-Wunused-variable\"")
#define __END_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic pop")
#endif

#define __END_IGNORE_UNUSEDVARS _Pragma("GCC diagnostic pop")

#define __BEGIN_IGNORE_TYPELIMITS _Pragma("GCC diagnostic push")    \
    _Pragma("GCC diagnostic ignored \"-Wtype-limits\"")
#define __END_IGNORE_TYPELIMITS _Pragma("GCC diagnostic pop")

#define __IGNORE_UNUSED_VAR(v) (void)v
#define __IGNORE_UNSUED_PVAR(v) (void*)v

void util_string_ltrim(std::string &s);
void util_string_rtrim(std::string &s);
void util_string_trim(std::string &s);

std::string util_tolower(const std::string& str);
void util_string_tolower(std::string &s);
void util_string_toupper(std::string &s);

long util_parseInt(FILE *f, char skipChar);
long util_parseInt(FILE *f);

unsigned char util_checksum(const char *chunk, int length);
std::string util_crunch(std::string filename);
std::string util_entry(std::string crunched, size_t fileSize, bool is_dir, bool is_locked);
std::string util_long_entry(std::string filename, size_t fileSize, bool is_dir);
std::string util_long_entry_apple2_80col(std::string filename, size_t fileSize, bool is_dir);
int util_ellipsize(const char* src, char *dst, int dstsize);
std::string util_ellipsize_string(const std::string& src, size_t maxSize);
bool util_wildcard_match(const char *str, const char *pattern);
bool util_starts_with(std::string s, const char *pattern);

bool util_concat_paths(char *dest, const char *parent, const char *child, int dest_size);

void util_dump_bytes(const uint8_t *buff, uint32_t buff_size);

std::vector<std::string> util_tokenize(std::string s, char c = ' ');
std::vector<uint8_t> util_tokenize_uint8(std::string s, char c = ' ');
std::string util_remove_spaces(const std::string &s);

void util_strip_nonascii(std::string &s);
void util_devicespec_fix_9b(uint8_t* buf, unsigned short len);
std::string util_devicespec_fix_for_parsing(std::string deviceSpec, std::string prefix, bool is_directory_read, bool process_fs_dot);

void clean_transform_petscii_to_ascii(std::string& data);

bool util_string_value_is_true(std::string value);
bool util_string_value_is_true(const char *value);


void util_sam_say(const char *p,
                  bool phonetic=false,
                  bool sing=false,
                  unsigned char pitch=64,
                  unsigned char speed=96,
                  unsigned char mouth=128,
                  unsigned char throat=128);
void util_sam_say_number(unsigned char n);
void util_sam_say_swap_label();

void util_replaceAll(std::string& str, const std::string& from, const std::string& to);

//std::string util_get_canonical_path(char* path);
std::string util_get_canonical_path(std::string path);

char util_petscii_to_ascii(char c);
char util_ascii_to_petscii(char c);
void util_petscii_to_ascii_str(std::string &s);
void util_ascii_to_petscii_str(std::string &s);

// generic hex dump for debug output
std::string util_hexdump(const void *buf, size_t len);

// check if a double is very close to an integer
bool isApproximatelyInteger(double value, double tolerance = 1e-6);

// ensure string starts with a "/"
std::string prependSlash(const std::string& str);

#ifndef ESP_PLATFORM
// helper function for Debug_print* macros on fujinet-pc
void util_debug_printf(const char *fmt, ...);
#endif // !ESP_PLATFORM

char* util_strndup(const char* s, size_t n);

int get_value_or_default(const std::map<int, int>& map, int key, int default_value);

#endif // _FN_UTILS_H
