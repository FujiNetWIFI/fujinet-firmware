#ifndef HASH_H
#define HASH_H

#include <cstdint>
#include <cstdbool>
#include <vector>

#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/md5.h"


class Hash {
public:
    enum class Algorithm {
        UNKNOWN = -1, MD5, SHA1, SHA256, SHA512
    };

    void add_data(const std::string& data);
    void add_data(const std::vector<uint8_t>& data);
    std::vector<uint8_t> hash(Algorithm algorithm, bool is_hex);
    void compute(Algorithm algorithm, bool clear_data);
    uint8_t hash_length(Algorithm algorithm, bool is_hex);

    void init() { data_buffer.clear(); data_buffer.shrink_to_fit(); }
    static Algorithm to_algorithm(uint8_t value);

private:
    std::vector<uint8_t> data_buffer;

    uint8_t md5_output[16];
    uint8_t sha1_output[20];
    uint8_t sha256_output[32];
    uint8_t sha512_output[64];

    mbedtls_sha1_context _sha1;
    mbedtls_sha256_context _sha256;
    mbedtls_sha512_context _sha512;


};

extern Hash hasher;

#endif