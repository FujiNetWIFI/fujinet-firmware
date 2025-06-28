#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <cstdint>
#include <string>
#include <string_view>

#include <vector>

void copyString(const std::string& input, char *dst, size_t dst_size);

inline constexpr auto hash_djb2a(const std::string_view sv) {
    unsigned long hash{ 5381 };
    for (unsigned char c : sv) {
        hash = ((hash << 5) + hash) ^ c;
    }
    return hash;
}

inline constexpr auto operator"" _sh(const char *str, size_t len) {
    return hash_djb2a(std::string_view{ str, len });
}

namespace mstr {
    std::string drop(std::string str, size_t count);
    std::string dropLast(std::string str, size_t count);

    bool startsWith(std::string s, const char *pattern, bool case_sensitive = true);
    bool endsWith(std::string s, const char *pattern, bool case_sensitive = true);

    bool equals(std::string &s1, std::string &s2, bool case_sensitive = true);
    bool equals(std::string &s1, const char *s2, bool case_sensitive = true);
    bool equals(const char* s1, const char *s2, bool case_sensitive);
    bool contains(std::string &s1, const char *s2, bool case_sensitive = true);
    bool compare(std::string &s1, std::string &s2, bool case_sensitive = true); // s1 is Wildcard string, s2 is potential match

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

    std::string urlEncode(const std::string &s);

    std::string urlDecode(const std::string& s, bool alter_pluses);
    std::string urlDecode(const std::string& s);
    void urlDecode(char *s, size_t size, bool alter_pluses);
    void urlDecode(char *s, size_t size);

    std::string sha1(const std::string &s);

    // void toASCII(std::string &s);
    // void toPETSCII(std::string &s);
    std::string toUTF8(const std::string &petsciiInput);
    std::string toPETSCII2(const std::string &utfInputString);
    std::string toHex(const uint8_t *input, size_t size);
    std::string toHex(const std::string &input);

    bool isText(std::string &s);
    bool isNumeric(std::string &s);
    bool isNumeric(char *s);
    bool isA0Space(int ch);
    bool isJunk(std::string &s);
    void A02Space(std::string &s);

    std::string format(const char *format, ...);
    std::string formatBytes(uint64_t value);

    void cd(std::string &path, std::string newDir);
    std::string parent(std::string path, std::string plus = "");
    std::string localParent(std::string path, std::string plus);
}
#endif