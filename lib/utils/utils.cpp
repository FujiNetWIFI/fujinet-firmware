#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "utils.h"
#include "../../include/debug.h"
#include "../sam/samlib.h"

using namespace std;

// convert to lowercase (in place)
void util_string_tolower(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
}

// convert to uppercase (in place)
void util_string_toupper(std::string &s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
}

// trim from start (in place)
void util_string_ltrim(std::string &s)
{
    s.erase(
        s.begin(),
        std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
void util_string_rtrim(std::string &s)
{
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
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
    unsigned char cksum;
    char cksum_txt[3];

    // Remove spaces
    filename.erase(remove(filename.begin(), filename.end(), ' '), filename.end());

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
    std::for_each(basename.begin(), basename.end(), [](char &c) {
        c = ::toupper(c);
    });

    if (ext_pos != std::string::npos)
    {
        ext = "." + filename.substr(ext_pos + 1);
    }

    std::for_each(ext.begin(), ext.end(), [](char &c) {
        c = ::toupper(c);
    });

    if (basename_long.length() > 8)
    {
        cksum = util_checksum(basename_long.c_str(), basename_long.length());
        sprintf(cksum_txt, "%02X", cksum);
        basename[basename.length() - 2] = cksum_txt[0];
        basename[basename.length() - 1] = cksum_txt[1];
    }

    return basename + ext;
}

std::string util_entry(std::string crunched, size_t fileSize)
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

    returned_entry.replace(2, (basename.size() < 8 ? basename.size() : 8), basename);

    if (fileSize > 255744)
        sectors = 999;
    else
    {
        sectors = fileSize >> 8;
    }

    sprintf(tmp, "%03d", sectors);
    sectorStr = tmp;

    returned_entry.replace(14, 3, sectorStr);

    return returned_entry;
}

std::string util_long_entry(std::string filename, size_t fileSize)
{
    std::string returned_entry = "                                     ";
    std::string stylized_filesize;

    char tmp[8];

    // Double size of returned entry if > 30 chars.
    // Add EOL so SpartaDOS doesn't truncate record. grrr.
    if (filename.length() > 30)
        returned_entry += "\x9b" + returned_entry;

    returned_entry.replace(0, filename.length(), filename);

    if (fileSize > 1048576)
        sprintf(tmp, "%2dM", (fileSize >> 20));
    else if (fileSize > 1024)
        sprintf(tmp, "%4dK", (fileSize >> 10));
    else
        sprintf(tmp, "%4d", fileSize);

    stylized_filesize = tmp;

    returned_entry.replace(returned_entry.length() - stylized_filesize.length() - 1, stylized_filesize.length(), stylized_filesize);

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

/*
std::string util_ellipsize(std::string longString, int maxLength)
{
    size_t partSize = (maxLength - 3) >> 1; // size of left/right parts.
    std::string leftPart;
    std::string rightPart;

    if (longString.length() <= maxLength)
        return longString;

    leftPart = longString.substr(0, partSize);
    rightPart = longString.substr(longString.length() - partSize, longString.length());

    return leftPart + "..." + rightPart;
}
*/

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
        Debug_printf("_concat_paths parent takes up entire destination buffer: \"%s\"\n", parent);
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
        if (child[0] == '/' && child[0] == '\\')
            child++;

        int clen = strlcpy(dest + plen, child, dest_size - plen);

        // Verify we were able to copy the whole thing
        if (clen != strlen(child))
        {
            Debug_printf("_concat_paths parent + child larger than dest buffer: \"%s\", \"%s\"\n", parent, child);
            return false;
        }
    }

    return true;
}

void util_dump_bytes(uint8_t *buff, uint32_t buff_size)
{
    int bytes_per_line = 16;
    for (int j = 0; j < buff_size; j += bytes_per_line)
    {
        for (int k = 0; (k + j) < buff_size && k < bytes_per_line; k++)
            Debug_printf("%02X ", buff[k + j]);
        Debug_println();
    }
    Debug_println();
}

vector<string> util_tokenize(string s, char c)
{
    vector<string> tokens;
    stringstream ss(s);
    string token;

    while (getline(ss, token, ' '))
    {
        tokens.push_back(token);
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
    for (int i=0;i<s.size();i++)
    {
        if (s[i]>0x7F)
            s[i]=0x00;
    }
}

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
    char pitchs[4], speeds[4], mouths[4],throats[4]; // itoa temp vars

    // Convert to strings.
    itoa(pitch,pitchs,10);
    itoa(speed,speeds,10);
    itoa(mouth,mouths,10);
    itoa(throat,throats,10);

    memset(a, 0, sizeof(a));
    a[n++] = (char *)("sam"); // argv[0] for compatibility

    if (phonetic==true)
        a[n++] = (char *)("-phonetic");
    
    if (sing==true)
        a[n++] = (char *)("-sing");

    a[n++]=(char *)("-pitch");
    a[n++]=(char *)pitchs;

    a[n++]=(char *)("-speed");
    a[n++]=(char *)speeds;

    a[n++]=(char *)("-mouth");
    a[n++]=(char *)mouths;

    a[n++]=(char *)("-throat");
    a[n++]=(char *)throats;

    // Append the phrase to say.
    a[n++] = (char *)p;
    sam(n, a);
}