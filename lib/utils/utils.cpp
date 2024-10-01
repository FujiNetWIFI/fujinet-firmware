
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <stack>
#include <string>

#include "compat_string.h"

#ifndef ESP_PLATFORM
#include <cstdarg>
#include "compat_gettimeofday.h"
#endif

#include "../../include/debug.h"
#include "string_utils.h"

#include "samlib.h"

using namespace std;

// non destructive version of lowercase conversion
std::string util_tolower(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return lower_str;
}

// convert to lowercase (in place)
void util_string_tolower(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
}

// convert to uppercase (in place)
void util_string_toupper(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return std::toupper(c); });
}

// trim from start (in place)
void util_string_ltrim(std::string &s)
{
    s.erase(
        s.begin(),
        std::find_if(s.begin(), s.end(), [](int ch)
                     { return !std::isspace(ch); }));
}

// trim from end (in place)
void util_string_rtrim(std::string &s)
{
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [](int ch)
                     { return !std::isspace(ch); })
            .base(),
        s.end());
}

// trim from both ends (in place)
void util_string_trim(std::string &s)
{
    util_string_ltrim(s);
    util_string_rtrim(s);
}

int _util_peek(FILE *f)
{
    int c = fgetc(f);
    fseek(f, -1, SEEK_CUR);
    return c;
}

// discards non-numeric characters
int _util_peekNextDigit(FILE *f)
{
    int c;
    while (1)
    {
        c = _util_peek(f);
        if (c < 0)
        {
            return c; // timeout
        }
        if (c == '-')
        {
            return c;
        }
        if (c >= '0' && c <= '9')
        {
            return c;
        }
        fgetc(f); // discard non-numeric
    }
}

long util_parseInt(FILE *f)
{
    return util_parseInt(f, 1); // terminate on first non-digit character (or timeout)
}

// as above but a given skipChar is ignored
// this allows format characters (typically commas) in values to be ignored
long util_parseInt(FILE *f, char skipChar)
{
    bool isNegative = false;
    long value = 0;
    int c;

    c = _util_peekNextDigit(f);
    // ignore non numeric leading characters
    if (c < 0)
    {
        return 0; // zero returned if timeout
    }

    do
    {
        if (c == skipChar)
        {
        } // ignore this charactor
        else if (c == '-')
        {
            isNegative = true;
        }
        else if (c >= '0' && c <= '9')
        { // is c a digit?
            value = value * 10 + c - '0';
        }
        fgetc(f); // consume the character we got with peek
        c = _util_peek(f);
    } while ((c >= '0' && c <= '9') || c == skipChar);

    if (isNegative)
    {
        value = -value;
    }
    return value;
}

// Calculate 8-bit checksum
unsigned char util_checksum(const char *chunk, int length)
{
    int chkSum = 0;
    for (int i = 0; i < length; i++)
    {
        chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
    }
    return (unsigned char)chkSum;
}

std::string util_crunch(std::string filename)
{
    std::string basename_long;
    std::string basename;
    std::string ext;
    size_t base_pos = 8;
    size_t ext_pos;
    std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.";
    unsigned char cksum;
    char cksum_txt[3];

    // Remove spaces
    filename.erase(remove(filename.begin(), filename.end(), ' '), filename.end());

    // remove unwanted characters
    filename.erase(remove_if(filename.begin(), filename.end(), [&chars](const char &c)
                             { return chars.find(c) == string::npos; }),
                   filename.end());

    ext_pos = filename.find_last_of(".");

    if (ext_pos != std::string::npos)
    {
        if (ext_pos > 8)
            base_pos = 8;
        else
            base_pos = ext_pos;
    }

    if (ext_pos != std::string::npos)
    {
        basename_long = filename.substr(0, ext_pos);
    }
    else
    {
        basename_long = filename;
    }

    basename = basename_long.substr(0, base_pos);

    // Convert to uppercase
    std::for_each(basename.begin(), basename.end(), [](char &c)
                  { c = ::toupper(c); });

    if (ext_pos != std::string::npos)
    {
        ext = "." + filename.substr(ext_pos + 1);
    }

    std::for_each(ext.begin(), ext.end(), [](char &c)
                  { c = ::toupper(c); });

    if (basename_long.length() > 8)
    {
        cksum = util_checksum(basename_long.c_str(), basename_long.length());
        sprintf(cksum_txt, "%02X", cksum);
        basename[basename.length() - 2] = cksum_txt[0];
        basename[basename.length() - 1] = cksum_txt[1];
    }

    return basename + ext;
}

std::string util_entry(std::string crunched, size_t fileSize, bool is_dir, bool is_locked)
{
    std::string returned_entry = "                 ";
    size_t ext_pos = crunched.find(".");
    std::string basename = crunched.substr(0, ext_pos);
    std::string ext = crunched.substr(ext_pos + 1);
    char tmp[4];
    unsigned short sectors;
    std::string sectorStr;

    if (ext_pos != std::string::npos)
    {
        returned_entry.replace(10, 3, ext.substr(0, 3));
    }

    if (is_dir == true)
    {
        returned_entry.replace(10, 3, "DIR");
        returned_entry.replace(0, 1, "/");
    }

    returned_entry.replace(2, (basename.size() < 8 ? basename.size() : 8), basename);

    if (fileSize > 255744)
        sectors = 999;
    else
    {
        sectors = fileSize >> 8;
        if (sectors == 0)
            sectors = 1; // at least 1 sector.
    }

    sprintf(tmp, "%03d", sectors);
    sectorStr = tmp;

    returned_entry.replace(14, 3, sectorStr);

    if (is_locked == true)
    {
        returned_entry.replace(0, 1, "*");
    }

    return returned_entry;
}

std::string util_long_entry(std::string filename, size_t fileSize, bool is_dir)
{
#ifdef BUILD_COCO
#define LONG_ENTRY_TRIM_LEN 25
#define LONG_ENTRY_EOL "\x0D"
    std::string returned_entry = "                               ";
#else
#define LONG_ENTRY_TRIM_LEN 30
#define LONG_ENTRY_EOL "\x9B"
    std::string returned_entry = "                                     ";
#endif /* BUILD_COCO */
    std::string stylized_filesize;

    char tmp[8];

    if (is_dir == true)
        filename += "/";

    // Double size of returned entry if > 30 chars.
    // Add EOL so SpartaDOS doesn't truncate record. grrr.
    if (filename.length() > LONG_ENTRY_TRIM_LEN)
        returned_entry += LONG_ENTRY_EOL + returned_entry;

    returned_entry.replace(0, filename.length(), filename);

    if (fileSize > 1048576)
        sprintf(tmp, "%2uM", (unsigned int)(fileSize >> 20));
    else if (fileSize > 1024)
        sprintf(tmp, "%4uK", (unsigned int)(fileSize >> 10));
    else
        sprintf(tmp, "%4u", (unsigned int)fileSize);

    stylized_filesize = tmp;

    returned_entry.replace(returned_entry.length() - stylized_filesize.length() - 1, stylized_filesize.length(), stylized_filesize);

    returned_entry.shrink_to_fit();
    
    return returned_entry;
}

const char *apple2_folder_icon(bool is_folder)
{
    if (is_folder)
        return "\xD8\xD9";
    else
        return "  ";
}

char apple2_fn[73];

const char *apple2_filename(std::string filename)
{
    util_ellipsize(filename.c_str(), apple2_fn, 68);
    return apple2_fn;
}

char apple2_fs[6];

const char *apple2_filesize(size_t fileSize)
{
    unsigned short fs = fileSize / 512;
#ifdef ESP_PLATFORM
     itoa(fs, apple2_fs, 10);
#else
    sprintf(apple2_fs, "%u", fs);
#endif
    return apple2_fs;
}

char tmp[81];

std::string util_long_entry_apple2_80col(std::string filename, size_t fileSize, bool is_dir)
{
    std::string returned_entry;
    std::string stylized_filesize;

    memset(tmp, 0, sizeof(tmp));

    sprintf(tmp, "%s %-70s %5s",
            apple2_folder_icon(is_dir),
            apple2_filename(filename),
            apple2_filesize(fileSize));

    returned_entry = string(tmp, 80);
    return returned_entry;
}

/* Shortens the source string by splitting it in to shorter halves connected by "..." if it won't fit in the destination buffer.
   Returns number of bytes copied into buffer.
*/
int util_ellipsize(const char *src, char *dst, int dstsize)
{
    // Don't do much if there's no space to copy anything
    if (dstsize <= 1)
    {
        if (dstsize == 1)
            dst[0] = '\0';
        return 0;
    }

    int srclen = strlen(src);

    // Do a simple copy if we have the room for it (or if we don't have room to create a string with ellipsis in the middle)
    if (srclen < dstsize || dstsize < 6)
    {
        return strlcpy(dst, src, dstsize);
    }

    // Account for both the 3-character ellipses and the null character that needs to fit in the destination
    int rightlen = (dstsize - 4) / 2;
    // The left side gets one more character if the destination is odd
    int leftlen = rightlen + dstsize % 2;

    strlcpy(dst, src, leftlen + 1); // Add one because strlcpy wants to add its own NULL

    dst[leftlen] = dst[leftlen + 1] = dst[leftlen + 2] = '.';

    strlcpy(dst + leftlen + 3, src + (srclen - rightlen), rightlen + 1); // Add one because strlcpy wants to add its own NULL

    return dstsize;
}

std::string util_ellipsize_string(const std::string& src, size_t maxSize) {
    if (src.length() <= maxSize) {
        return src;
    }
    
    if (maxSize < 6) { // Not enough space for ellipsis in the middle
        return src.substr(0, maxSize);
    }

    size_t leftLen = (maxSize - 3) / 2; // 3 for ellipsis
    size_t rightLen = maxSize - 3 - leftLen;
    return src.substr(0, leftLen) + "..." + src.substr(src.length() - rightLen);
}

// Function that matches input string against given wildcard pattern
bool util_wildcard_match(const char *str, const char *pattern)
{
    if (str == nullptr || pattern == nullptr)
        return false;

    int m = strlen(pattern);
    int n = strlen(str);

    // Empty pattern can only match with empty string
    if (m == 0)
        return (n == 0);

    // Lookup table for storing results of subproblems
    bool lookup[n + 1][m + 1];

    // Initailze lookup table to false
    memset(lookup, false, sizeof(lookup));

    // Empty pattern can match with empty string
    lookup[0][0] = true;

    // Only '*' can match with empty string
    for (int j = 1; j <= m; j++)
        if (pattern[j - 1] == '*')
            lookup[0][j] = lookup[0][j - 1];

    // Fill the table in bottom-up fashion
    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= m; j++)
        {
            // Two cases if we see a '*':
            // a) We ignore '*' character and move to next character in the pattern,
            //     i.e., '*' indicates an empty sequence.
            // b) '*' character matches with i-th character in input
            if (pattern[j - 1] == '*')
            {
                lookup[i][j] = lookup[i][j - 1] || lookup[i - 1][j];
            }
            // Current characters are considered as matching in two cases:
            // (a) Current character of pattern is '?'
            // (b) Characters actually match (case-insensitive)
            else if (pattern[j - 1] == '?' ||
                     str[i - 1] == pattern[j - 1] ||
                     tolower(str[i - 1]) == tolower(pattern[j - 1]))
            {
                lookup[i][j] = lookup[i - 1][j - 1];
            }
            // If characters don't match
            else
                lookup[i][j] = false;
        }
    }

    return lookup[n][m];
}

bool util_starts_with(std::string s, const char *pattern)
{
    if (s.empty() || pattern == nullptr)
        return false;

    std::string ss = s.substr(0, strlen(pattern));
    return ss.compare(pattern) == 0;
}

/*
 Concatenates two paths by taking the parent and adding the child at the end.
 If parent is not empty, then a '/' is confirmed to separate the parent and child.
 Results are copied into dest.
 FALSE is returned if the buffer is not big enough to hold the two parts.
*/
bool util_concat_paths(char *dest, const char *parent, const char *child, int dest_size)
{
    if (dest == nullptr)
        return false;

    // If parent is null or empty, just copy the chlid into the destination as-is
    if (parent == nullptr || parent[0] == '\0')
    {
        if (child == nullptr)
            return false;

        int l = strlen(child);

        return l == strlcpy(dest, child, dest_size);
    }

    // Copy the parent string in first
    int plen = strlcpy(dest, parent, dest_size);

    // Make sure we have room left after copying the parent
    if (plen >= dest_size - 3) // Allow for a minimum of a slash, one char, and NULL
    {
        Debug_printf("_concat_paths parent takes up entire destination buffer: \"%s\"\r\n", parent);
        return false;
    }

    if (child != nullptr && child[0] != '\0')
    {
        // Add a slash if the parent didn't end with one
        if (dest[plen - 1] != '/' && dest[plen - 1] != '\\')
        {
            dest[plen++] = '/';
            dest[plen] = '\0';
        }

        // Skip a slash in the child if it starts with one so we don't have two slashes
        if (child[0] == '/' || child[0] == '\\')
            child++;

        int clen = strlcpy(dest + plen, child, dest_size - plen);

        // Verify we were able to copy the whole thing
        if (clen != strlen(child))
        {
            Debug_printf("_concat_paths parent + child larger than dest buffer: \"%s\", \"%s\"\r\n", parent, child);
            return false;
        }
    }

    return true;
}

void util_dump_bytes(const uint8_t *buff, uint32_t buff_size)
{
    int bytes_per_line = 16;
    for (int j = 0; j < buff_size; j += bytes_per_line)
    {
        for (int k = 0; (k + j) < buff_size && k < bytes_per_line; k++)
            Debug_printf("%02X ", buff[k + j]);
        Debug_println("");
    }
    Debug_println("");
}

vector<string> util_tokenize(string s, char c)
{
    vector<string> tokens;
    stringstream ss(s);
    string token;

    while (getline(ss, token, c))
    {
        tokens.push_back(token);
    }

    return tokens;
}

vector<uint8_t> util_tokenize_uint8(string s, char c)
{
    vector<uint8_t> tokens;
    stringstream ss(s);
    string token;

    while (getline(ss, token, c))
    {
        tokens.push_back( atoi(token.c_str()) );
    }

    return tokens;
}

string util_remove_spaces(const string &s)
{
    int last = s.size() - 1;
    while (last >= 0 && s[last] == ' ')
        --last;
    return s.substr(0, last + 1);
}

void util_strip_nonascii(string &s)
{
    for (int i = 0; i < s.size(); i++)
    {
        if ((unsigned char)s[i] > 0x7F)
            s[i] = 0x00;
    }
}

void util_devicespec_fix_9b(uint8_t *buf, unsigned short len)
{
    for (int i = 0; i < len; i++)
        if (buf[i] == 0x9b)
            buf[i] = 0x00;
}

// does 3 things:
// 1. replace 0xa4 with 0x5f (underscore char)
// 2. removes final 0x9b chars that may be coming out the host because of x-platform code
// 3. converts petscii to ascii
void clean_transform_petscii_to_ascii(std::string& data) {
    // 1. Replace all chars of value 0xa4 to 0x5f (the dreaded underscore)
    std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) {
        return c == 0xa4 ? 0x5f : c;
    });

    // 2. Remove any trailing 0x9b chars
    while (!data.empty() && static_cast<unsigned char>(data.back()) == 0x9b) {
        data.pop_back();
    }

    // 3. Convert the characters from PETSCII to UTF8
    data = mstr::toUTF8(data);
}

// Non-mutating
std::string util_devicespec_fix_for_parsing(std::string deviceSpec, std::string prefix, bool is_directory_read, bool process_fs_dot)
{
    if (deviceSpec.length() == 0) {
        Debug_printv("ERROR: deviceSpec is empty, returning empty string");
        return "";
    }

    string unit = deviceSpec.substr(0, deviceSpec.find_first_of(":") + 1);
    // if prefix is empty, the concatenation is still valid
    deviceSpec = unit + prefix + deviceSpec.substr(deviceSpec.find(":") + 1);

#ifdef VERBOSE_PROTOCOL
    Debug_printf("util_devicespec_fix_for_parsing, spec: >%s<, prefix: >%s<, dir_read?: %s, fs_dot?: %s)\n", deviceSpec.c_str(), prefix.c_str(), is_directory_read ? "true" : "false", process_fs_dot ? "true" : "false");
#endif

    util_strip_nonascii(deviceSpec);

    if (!is_directory_read) // Anything but a directory read...
    {
        replace(deviceSpec.begin(), deviceSpec.end(), '*', '\0'); // FIXME: Come back here and deal with WC's
    }

    // Some FMSes add a dot at the end, remove it if required to. Only seems to be SIO that uses this code, but we'll control its use through a default parameter
    if (process_fs_dot && deviceSpec.substr(deviceSpec.length() - 1) == ".")
    {
        deviceSpec.erase(deviceSpec.length() - 1, string::npos);
    }

    // Remove any spurious spaces
    deviceSpec = util_remove_spaces(deviceSpec);

    return deviceSpec;
}

bool util_string_value_is_true(const char *value)
{
    switch (value ? value[0] : '\0')
    {
        case '1':
        case 'T':
        case 't':
        case 'Y':
        case 'y':
            return true;
        default:
            return false;
    }
}

bool util_string_value_is_true(std::string value)
{
    return util_string_value_is_true(value.c_str());
}

#ifdef BUILD_ATARI
/**
 * Ask SAM to say something. see https://github.com/FujiNetWIFI/fujinet-platformio/wiki/Using-SAM-%28Voice-Synthesizer%29
 * @param p The phrase to say.
 * @param phonetic true = enable phonetic mode.
 * @param sing true = enable singing mode.
 * @param pitch Sam's pitch. (1-255) Lower values = Higher pitch. Default is 64. Values below 20 are unusable.
 * @param speed Sam's speed. (1-255) Lower values = higher speed. Default is 72. Values below 20 are usuable.
 * @param mouth The emphasis of transient sounds (1-255), Higher values imply more pronounced mouth movement. Default is 128.
 * @param throat The size of throat, changes resonance of formant sounds (1-255), higher values imply a deeper throat. Default is 128.
 */
void util_sam_say(const char *p,
                  bool phonetic,
                  bool sing,
                  unsigned char pitch,
                  unsigned char speed,
                  unsigned char mouth,
                  unsigned char throat)
{
    int n = 0;
    char *a[20];
    char pitchs[4], speeds[4], mouths[4], throats[4]; // itoa temp vars

    // Convert to strings.
#ifdef ESP_PLATFORM
    itoa(pitch, pitchs, 10);
    itoa(speed, speeds, 10);
    itoa(mouth, mouths, 10);
    itoa(throat, throats, 10);
#else
    sprintf(pitchs, "%u", pitch);
    sprintf(speeds, "%u", speed);
    sprintf(mouths, "%u", mouth);
    sprintf(throats, "%u", throat);
#endif

    memset(a, 0, sizeof(a));
    a[n++] = (char *)("sam"); // argv[0] for compatibility

    if (phonetic == true)
        a[n++] = (char *)("-phonetic");

    if (sing == true)
        a[n++] = (char *)("-sing");

    a[n++] = (char *)("-pitch");
    a[n++] = (char *)pitchs;

    a[n++] = (char *)("-speed");
    a[n++] = (char *)speeds;

    a[n++] = (char *)("-mouth");
    a[n++] = (char *)mouths;

    a[n++] = (char *)("-throat");
    a[n++] = (char *)throats;

    // Append the phrase to say.
    a[n++] = (char *)p;
#ifdef ESP_PLATFORM
    sam(n, a);
#endif
}

/**
 * Say the numbers 1-8 using phonetic tweaks.
 * @param n The number to say.
 */
void util_sam_say_number(unsigned char n)
{
    switch (n)
    {
    case 1:
        util_sam_say("WAH7NQ", true);
        break;
    case 2:
        util_sam_say("TUW7", true);
        break;
    case 3:
        util_sam_say("THRIYY7Q", true);
        break;
    case 4:
        util_sam_say("FOH7R", true);
        break;
    case 5:
        util_sam_say("F7AYVQ", true);
        break;
    case 6:
        util_sam_say("SIH7IHKSQ", true);
        break;
    case 7:
        util_sam_say("SEHV7EHNQ", true);
        break;
    case 8:
        util_sam_say("AEY74Q", true);
        break;
    default:
        Debug_printf("say_number() - Uncaught number %d", n);
    }
}

/**
 * Say swap label
 */
void util_sam_say_swap_label()
{
    // DISK
    util_sam_say("DIHSK7Q ", true);
}
#endif

void util_replaceAll(std::string &str, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

/**
 * Resolve path containing relative references (../ or ./)
 * to canonical path.
 * @param path FujiNet path such as TNFS://myhost/path/to/here/
 * or tnfs://myhost/some/filename.ext
 */
std::string util_get_canonical_path(std::string path)
{
    bool is_last_slash = (path.back() == '/') ? true : false;

    std::size_t proto_host_len;

    // stack to store the file's names.
    std::stack<std::string> st;

    // temporary string which stores the extracted
    // directory name or commands("." / "..")
    // Eg. "/a/b/../."
    // dir will contain "a", "b", "..", ".";
    std::string dir;

    // contains resultant simplifies string.
    std::string res;

    // advance beyond protocol and hostname
    proto_host_len = path.find("://");

    // If protocol delimiter "://" is found, skip over the protocol
    if (proto_host_len < std::string::npos)
    {
        proto_host_len += 3; // "://" is 3 chars
        proto_host_len = path.find("/", proto_host_len) + 1;
        res.append(path.substr(0, proto_host_len));
    }
    else
    {
        proto_host_len = 0; // no protocol prefix and hostname
        // Preserve an absolute path if one is provided in the input
        if (!path.empty() && path[0] == '/')
            res = "/";
    }

    int len_path = path.length();

    for (int i = proto_host_len; i < len_path; i++)
    {
        // we will clear the temporary string
        // every time to accommodate new directory
        // name or command.
        dir.clear();

        // skip all the multiple '/' Eg. "/////""
        while (path[i] == '/')
            i++;

        // stores directory's name("a", "b" etc.)
        // or commands("."/"..") into dir
        while (i < len_path && path[i] != '/')
        {
            dir.push_back(path[i]);
            i++;
        }

        // if dir has ".." just pop the topmost
        // element if the stack is not empty
        // otherwise ignore.
        if (dir.compare("..") == 0)
        {
            if (!st.empty())
                st.pop();
        }

        // if dir has "." then simply continue
        // with the process.
        else if (dir.compare(".") == 0)
            continue;

        // pushes if it encounters directory's
        // name("a", "b").
        else if (dir.length() != 0)
            st.push(dir);
    }

    // a temporary stack  (st1) which will contain
    // the reverse of original stack(st).
    std::stack<std::string> st1;
    while (!st.empty())
    {
        st1.push(st.top());
        st.pop();
    }

    // the st1 will contain the actual res.
    while (!st1.empty())
    {
        std::string temp = st1.top();

        // if it's the last element no need
        // to append "/"
        if (st1.size() != 1)
            res.append(temp + "/");
        else
            res.append(temp);

        st1.pop();
    }

    // Append trailing slash if not already there
    if ((res.length() > 0) && (res.back() != '/') && is_last_slash)
        res.append("/");

    return res;
}

char util_petscii_to_ascii(char c)
{
    if ((c > 0x40) && (c < 0x5B))
        c += 0x20;
    else if ((c > 0x60) && (c < 0x7B))
        c -= 0x20;

    return c;
}

char util_ascii_to_petscii(char c)
{
    if ((c > 0x40) && (c < 0x5B))
        c -= 0x20;
    else if ((c > 0x60) && (c < 0x7B))
        c += 0x20;

    return c;
}

void util_petscii_to_ascii_str(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return util_petscii_to_ascii(c); });
}

void util_ascii_to_petscii_str(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return util_ascii_to_petscii(c); });
}

std::string util_hexdump(const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    std::string result;
    char line[100], ascii[17] = {};
    size_t i, idx = 16; // Initialize idx to 16 to handle case when len is 0

    for (i = 0; i < len; ++i) {
        idx = i % 16;
        if (idx == 0) {
            if (i != 0) {
                snprintf(line, sizeof(line), "  %s\n", ascii);
                result += line;
            }
            snprintf(line, sizeof(line), "%04x ", (unsigned int)i);
            result += line;
        }

        snprintf(line, sizeof(line), " %02x", p[i]);
        result += line;

        ascii[idx] = (isprint(p[i]) ? p[i] : '.');
        ascii[idx + 1] = '\0';
    }

    // This block adjusts the final line if len is not a multiple of 16
    if (len % 16) {
        for (size_t j = idx + 1; j < 16; ++j) {
            result += "   ";
        }
    }

    // Append the final ASCII representation, if len > 0
    if (len != 0) {
        snprintf(line, sizeof(line), "  %s\n", ascii);
        result += line;
    }

    return result;
}

bool isApproximatelyInteger(double value, double tolerance) {
    return std::abs(value - std::floor(value)) < tolerance;
}

std::string prependSlash(const std::string& str) {
    if (str.empty() || str[0] != '/') {
        return "/" + str;
    }
    return str;
}

#ifndef ESP_PLATFORM
// helper function for Debug_print* macros on fujinet-pc
void util_debug_printf(const char *fmt, ...)
{
    static bool print_ts = true;
    va_list argp;

    if (!print_ts)
    {
        if (fmt != nullptr)
        {
            print_ts = fmt[strlen(fmt)-1] == '\n';
        }
        else
        {
            va_start(argp, fmt);
            const char *s = va_arg(argp, const char*);
            print_ts = s[strlen(s)-1] == '\n';
            va_end(argp);
        }
        if (print_ts)
            printf("\n");
    }

    if (print_ts) 
    {
        // printf("DEBUG > ");
        timeval tv;
        tm tm;
        char buffer[32];

        compat_gettimeofday(&tv, NULL);
#if defined(_WIN32)
        time_t t = (time_t)tv.tv_sec;
        localtime_s(&tm, &t);
#else
        localtime_r(&tv.tv_sec, &tm);
#endif
        size_t endpos = strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
        snprintf(buffer + endpos, sizeof(buffer) - endpos, ".%06d", (int)(tv.tv_usec));
        printf("%s > ", buffer);
    }

    va_start(argp, fmt);
    if (fmt != nullptr)
    {
        print_ts = fmt[strlen(fmt)-1] == '\n';
        vprintf(fmt, argp);
    }
    else
    {
        const char *s = va_arg(argp, const char*);
        print_ts = s[strlen(s)-1] == '\n';
        printf("%s", s);
    }
    va_end(argp);
    fflush(stdout);
}
#endif // !ESP_PLATFORM

char* util_strndup(const char* s, size_t n) {
    // Find the length of the string up to n characters
    size_t len = strnlen(s, n);
    // Allocate memory for the new string
    char* new_str = (char*)malloc(len + 1);
    if (new_str == NULL) {
        // Allocation failed
        return NULL;
    }
    // Copy the string into the new memory and null-terminate it
    memcpy(new_str, s, len);
    new_str[len] = '\0';
    return new_str;
}

int get_value_or_default(const std::map<int, int>& map, int key, int default_value) {
    auto it = map.find(key);
    return it != map.end() ? it->second : default_value;
}
