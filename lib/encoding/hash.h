#ifndef HASH_H
#define HASH_H

#include <cstdint>
#include <vector>

#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/md5.h"


class Hash {
public:
    unsigned char md5_output[16];
    unsigned char sha1_output[20];
    unsigned char sha256_output[32];
    unsigned char sha512_output[64];

    mbedtls_sha1_context _sha1;
    mbedtls_sha256_context _sha256;
    mbedtls_sha512_context _sha512;

    std::vector<uint8_t> hash_output(uint16_t m, char hash_mode, uint16_t& olen);
    void compute(uint16_t m, const std::string& data);

};

extern Hash hasher;

#endif