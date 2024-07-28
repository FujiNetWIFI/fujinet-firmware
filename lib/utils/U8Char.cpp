#include "U8Char.h"
#include "punycode.h"

// from https://style64.org/petscii/

// PETSCII table in UTF8,  non-mappable characters mapped to Private Use Area E000-F8FF
const char16_t U8Char::utf8map[] = {
// we can't touch standard ASCII (<127), even for codes imssing in PETSCII, as this will cause all kinds of problems
//  ---0,   ---1,   ---2,   ---3,   ---4,   ---5,   ---6,   ---7,   ---8,   ---9,   --10,   --11,   --12,   --13,   --14,   --15    
    0x00,   0x01,   0x02,   0x03,   0x04,   0x05,   0x06,   0x07,   0x08,   0x09,   0x0a,   0x0b,   0x0c,   0x0d,   0x0e,   0x0f,  // ASCII control codes
    0x10,   0x11,   0x12,   0x13,   0x14,   0x15,   0x16,   0x17,   0x18,   0x19,   0x1a,   0x1b,   0x1c,   0x1d,   0x1e,   0x1f,  // ASCII control codes
    0x20,   0x21,   0x22,   0x23,   0x24,   0x25,   0x26,   0x27,   0x28,   0x29,   0x2a,   0x2b,   0x2c,   0x2d,   0x2e,   0x2f,  // punct
    0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,   0x38,   0x39,   0x3a,   0x3b,   0x3c,   0x3d,   0x3e,   0x3f,  // numbers

    0x40, // @
    0x61,   0x62,   0x63,   0x64,   0x65,   0x66,   0x67,   0x68,   0x69,   0x6a,   0x6b,   0x6c,   0x6d,   0x6e,   0x6f,  // a-o
    0x70,   0x71,   0x72,   0x73,   0x74,   0x75,   0x76,   0x77,   0x78,   0x79,   0x7a,   // p-z
    0x5b,   0x5c,   0x5d,   0x5e,   0x5f, // [ \ ] ^ _

    0x60, // `
    0x41,   0x42,   0x43,   0x44,   0x45,   0x46,   0x47,   0x48,   0x49,   0x4a,   0x4b,   0x4c,   0x4d,   0x4e,   0x4f,  // A-O
    0x50,   0x51,   0x52,   0x53,   0x54,   0x55,   0x56,   0x57,   0x58,   0x59,   0x5a, // P-Z
    0x7b,   0x7c,   0x7d,   0x7e,   0x7f, // { | } ~ DEL

  0xE015, 0xE016, 0xE017, 0xE018, 0xE019, 0xE01A, 0xE01B, 0xE01C, 0xE01D, 0xE01E, 0xE01F, 0xE020, 0xE021, 0x2028, 0xE022, 0xE023,  // PETSCII control codes
  0xE024, 0xE025, 0xE026, 0xE027, 0xE028, 0xE029, 0xE02A, 0xE02B, 0xE02C, 0xE02D, 0xE02E, 0xE02F, 0xE030, 0xE031, 0xE032, 0xE033,  // PETSCII control codes

    0xa0, 0x258c, 0x2584, 0x2594, 0x2581, 0x258e, 0x2592, 0xE034, 0xE035, 0xE036, 0xE037, 0x251c, 0x2597, 0x2514, 0x2510, 0x2582,  // PETSCII tables etc.
  0x250c, 0x2534, 0x252c, 0x2524, 0x258e, 0x258d, 0xE038, 0xE039, 0xE03A, 0x2583, 0x2713, 0x2596, 0x259d, 0x2518, 0x2598, 0x259a,  // PETSCII tables etc.
  0x2500,   0x41,   0x42,   0x43,   0x44,   0x45,   0x46,   0x47,   0x48,   0x49,   0x4a,   0x4b,   0x4c,   0x4d,   0x4e,   0x4f,  // A-Z again
    0x50,   0x51,   0x52,   0x53,   0x54,   0x55,   0x56,   0x57,   0x58,   0x59,   0x5a, 0x253c, 0xE03B, 0x2502, 0xE03C, 0xE03D,  
    0xa0, 0x258c, 0x2584, 0x2594, 0x2581, 0x258e, 0x2592, 0xE03F, 0xE040, 0xE041, 0xE042, 0x251c, 0x2597, 0x2514, 0x2510, 0x2582,  // PETSCII tables etc.
  0x250c, 0x2534, 0x252c, 0x2524, 0x258e, 0x258d, 0xE043, 0xE044, 0xE045, 0x2583, 0x2713, 0x2596, 0x259d, 0x2518, 0x2598, 0xE046   // PETSCII tables etc.

};

// std::unordered_map<char16_t, uint8_t> U8Char::ch_to_petascii_map;
// std::once_flag U8Char::ch_to_petascii_init_flag;

void U8Char::fromUtf8Stream(std::istream* reader) {
    uint8_t byte = reader->get();
    if(byte<=0x7f) {
        ch = byte;
    }   
    else if((byte & 0b11100000) == 0b11000000) {
        uint16_t hi =  ((uint16_t)(byte & 0b1111)) << 6;
        uint16_t lo = (reader->get() & 0b111111);
        ch = hi | lo;
    }
    else if((byte & 0b11110000) == 0b11100000) {
        uint16_t hi = ((uint16_t)(byte & 0b111)) << 12;
        uint16_t mi = ((uint16_t)(reader->get() & 0b111111)) << 6;
        uint16_t lo = reader->get() & 0b111111;
        ch = hi | mi | lo;
    }
    else {
        ch = 0;
    }
};

size_t U8Char::fromCharArray(char* reader) {
    uint8_t byte = reader[0];
    if(byte<=0x7f) {
        ch = byte;
        return 1;
    }   
    else if((byte & 0b11100000) == 0b11000000) {
        uint16_t hi =  ((uint16_t)(byte & 0b1111)) << 6;
        uint16_t lo = (reader[1] & 0b111111);
        ch = hi | lo;
        return 2;
    }
    else if((byte & 0b11110000) == 0b11100000) {
        uint16_t hi = ((uint16_t)(byte & 0b111)) << 12;
        uint16_t mi = ((uint16_t)(reader[1] & 0b111111)) << 6;
        uint16_t lo = reader[2] & 0b111111;
        ch = hi | mi | lo;
        return 3;
    }
    else {
        ch = 0;
        return 1;
    }
};

std::string U8Char::toUtf8() {
    if(ch==0) {
        return std::string(1,  missing);
    }
    else if(ch>=0x01 && ch<=0x7f) {
        // For code points in the range 0x0000 to 0x007F (1-byte sequences):
        // Directly represent the code point in binary.
        // Format: 0xxxxxxx
        return std::string(1,  char(ch));
    }
    else if(ch>=0x80 && ch<=0x7ff) {
        // First byte: 110xxxxx,  where the x's are the first 5 bits of the code point.
        char upper = (uint8_t)((ch>>6) & 0b11111) | 0b11000000; 
        // Second byte: 10xxxxxx,  where the x's are the next 6 bits of the code point.
        auto lower = (uint8_t)(ch & 0b111111) | 0b10000000; 
        char arr[] = { (char)upper,  (char)lower,  '\0'};
        return std::string(arr);
    }
    else {
        // First byte: 1110xxxx,  where the x's are the first 4 bits of the code point.
        auto hi = (uint8_t)((ch>>12) & 0b00001111) | 0b11100000;
        // Second byte: 10xxxxxx,  where the x's are the next 6 bits.
        auto mid = (uint8_t)((ch>>6) & 0b00111111) | 0b10000000;
        // Third byte: 10xxxxxx,  where the x's are the last 6 bits.
        auto lower = (uint8_t)(ch & 0b00111111) | 0b10000000;
        char arr[] = { (char)hi,  (char)mid,  (char)lower,  '\0'};
        return std::string(arr);
    }
}

uint8_t U8Char::toPetscii() {
    // we only need to convert standard ascii values 0-255 to petscii, everything else is "missing". This only needs to be done in 2 ranges, no need for map
    if (ch > 255) return missing;

    uint8_t c = (uint8_t) ch;
    if ((c > 0x40) && (c < 0x5B))
        c += 0x20;
    else if ((c > 0x60) && (c < 0x7B))
        c -= 0x20;
    
    return c;
}

// for punycode we need utf8 converted to uint32_t 
// workflows:
// char* ascii_punycode -> uint32_t* -> char* utf8
// char* utf8 -> uint32_t* -> char* ascii_punycode

// convert utf8 encoded string to array of uint32_t, return length of output_unicode32
size_t U8Char::toUnicode32(std::string& input_utf8, uint32_t* output_unicode32, size_t max_output_length) {
    size_t input_length = input_utf8.length();
    size_t output_length = 0;
    size_t i = 0;
    char* asChar = (char *)input_utf8.c_str();

    while(i<input_length && output_length<max_output_length) {
        U8Char ch(' ');
        size_t skip = ch.fromCharArray(asChar+i);
        output_unicode32[output_length++] = ch.ch;
        i += skip;
    }
    return output_length;
}

// convert array of uint32_t to utf8 encoded string, return utf8 string
std::string U8Char::fromUnicode32(uint32_t* input_unicode32, size_t input_length) {
    std::string output_utf8;
    for(size_t i = 0; i<input_length; i++) {
        U8Char ch((uint16_t)input_unicode32[i]);
        output_utf8 += ch.toUtf8();
    }
    return output_utf8;
}

std::string U8Char::toPunycode(std::string utf8String) {
    uint32_t asU32[1024];
    char asPunycode[1024];
    size_t dstlen = sizeof asPunycode;
    size_t n_converted;
    U8Char temp(' ');

    size_t conv_len = temp.toUnicode32(utf8String, asU32, sizeof asU32);
    n_converted = punycode_encode(asU32, conv_len, asPunycode, &dstlen);    
    return std::string(asPunycode, n_converted);
}


std::string U8Char::fromPunycode(std::string punycodeString) {
    uint32_t asU32[1024];
    size_t dstlen = sizeof asU32;
    U8Char temp(' ');

    punycode_decode(punycodeString.c_str(), punycodeString.length(), asU32, &dstlen);
    return temp.fromUnicode32(asU32, dstlen);
}