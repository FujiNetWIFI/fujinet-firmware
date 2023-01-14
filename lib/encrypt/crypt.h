#ifndef FN_CRYPT_H
#define FN_CRYPT_H

#include <cstring>
#include <string>

// A simple, portable crypto class using routine
// from micro-emacs at https://github.com/torvalds/uemacs

// Only converts printable chars between ascii 32 and 126 (space to ~)
// back into printable characters for easy transmission.

// May have to worry about space at start/end of the encoding if the resultant string is saved.

class Crypto {
private:
    std::string _key;
    void myencrypt(char *bptr, unsigned len);
    int mod95(int);

public:
    // crypt/decrypt are isomorphic, and just reverse the process
    std::string crypt(std::string t);
    void setkey(std::string key) { _key = key; }
};

extern Crypto crypto;

#endif