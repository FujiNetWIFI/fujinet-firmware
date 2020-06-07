#include <algorithm>
#include <cstdio>
#include <cstring>

#include "utils.h"
// trim from start (in place)
void util_ltrim(std::string &s)
{
    s.erase(
        s.begin(),
        std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
void util_rtrim(std::string &s)
{
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

// trim from both ends (in place)
void util_trim(std::string &s)
{
    util_ltrim(s);
    util_rtrim(s);
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
        returned_entry.replace(10, 3, ext);
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
    std::string ellpisized_filename = util_ellipsize(filename, 30);
    std::string stylized_fileSize;
    char tmp[5];

    returned_entry.replace(0, 32, ellpisized_filename);

    if (fileSize > 1048576)
        sprintf(tmp, "%2dM", (fileSize >> 20));
    else if (fileSize > 1024)
        sprintf(tmp, "%4dK", (fileSize >> 10));
    else
        sprintf(tmp, "%4d", fileSize);

    stylized_fileSize = tmp;

    returned_entry.replace(37-stylized_fileSize.length(), stylized_fileSize.length(), stylized_fileSize);

    return returned_entry;
}

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

// Function that matches input str with
// given wildcard pattern
bool util_wildcard_match(char str[], char pattern[], int n, int m)
{
    // empty pattern can only match with
    // empty string
    if (m == 0)
        return (n == 0);

    // lookup table for storing results of
    // subproblems
    bool lookup[n + 1][m + 1];

    // initailze lookup table to false
    memset(lookup, false, sizeof(lookup));

    // empty pattern can match with empty string
    lookup[0][0] = true;

    // Only '*' can match with empty string
    for (int j = 1; j <= m; j++)
        if (pattern[j - 1] == '*')
            lookup[0][j] = lookup[0][j - 1];

    // fill the table in bottom-up fashion
    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= m; j++)
        {
            // Two cases if we see a '*'
            // a) We ignore ‘*’ character and move
            //    to next  character in the pattern,
            //     i.e., ‘*’ indicates an empty sequence.
            // b) '*' character matches with ith
            //     character in input
            if (pattern[j - 1] == '*')
                lookup[i][j] = lookup[i][j - 1] ||
                               lookup[i - 1][j];

            // Current characters are considered as
            // matching in two cases
            // (a) current character of pattern is '?'
            // (b) characters actually match
            else if (pattern[j - 1] == '?' ||
                     str[i - 1] == pattern[j - 1])
                lookup[i][j] = lookup[i - 1][j - 1];

            // If characters don't match
            else
                lookup[i][j] = false;
        }
    }

    return lookup[n][m];
}