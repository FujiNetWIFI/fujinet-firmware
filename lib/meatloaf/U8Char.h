#ifndef MEATLOAF_UTILS_U8CHAR
#define MEATLOAF_UTILS_U8CHAR

#include <string>
#include <iostream>

/********************************************************
 * U8Char
 * 
 * A minimal wide char implementation that can handle UTF8
 * and convert it to PETSCII
 ********************************************************/

class U8Char {
    static const char16_t utf8map[];
    const char missing = '?';
    void fromUtf8Stream(std::istream* reader);

public:
    char16_t ch;
    U8Char(uint16_t codepoint): ch(codepoint) {};
    U8Char(std::istream* reader) {
        fromUtf8Stream(reader);
    }
    U8Char(char petscii) {
        ch = utf8map[(uint8_t)petscii];
    }

    std::string toUtf8();
    uint8_t toPetscii();
};

#endif /* MEATLOAF_UTILS_U8CHAR */
