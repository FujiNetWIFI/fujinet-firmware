#include <algorithm>
#include <cstdio>

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
    while(1) {
        c = _util_peek(f);
        if(c < 0) {
            return c;    // timeout
        }
        if(c == '-') {
            return c;
        }
        if(c >= '0' && c <= '9') {
            return c;
        }
        fgetc(f);  // discard non-numeric
    }
}

long util_parseInt(FILE * f)
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
    if(c < 0) {
        return 0;    // zero returned if timeout
    }

    do {
        if(c == skipChar) {
        } // ignore this charactor
        else if(c == '-') {
            isNegative = true;
        } else if(c >= '0' && c <= '9') {    // is c a digit?
            value = value * 10 + c - '0';
        }
        fgetc(f);  // consume the character we got with peek
        c = _util_peek(f);
    } while((c >= '0' && c <= '9') || c == skipChar);

    if(isNegative) {
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
      basename_long = filename.substr(0,ext_pos);
    }
  else
    {
      basename_long = filename;
    }

  basename = basename_long.substr(0,base_pos);


  // Convert to uppercase
  std::for_each(basename.begin(), basename.end(), [](char & c){
                                                    c = ::toupper(c);
                                                  });

  if (ext_pos != std::string::npos)
    {
      ext = "." + filename.substr(ext_pos+1);
    }

  std::for_each(ext.begin(), ext.end(), [](char & c){
                                          c = ::toupper(c);
                                        });

  if (basename_long.length()>8)
    {
      cksum = util_checksum(basename_long.c_str(),basename_long.length());
      sprintf(cksum_txt,"%02X",cksum);
      basename[basename.length()-2]=cksum_txt[0];
      basename[basename.length()-1]=cksum_txt[1];
    }

  return basename + ext;
}
