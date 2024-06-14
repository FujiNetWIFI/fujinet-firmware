#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "hash.h"

Hash hasher;

Hash::Algorithm Hash::to_algorithm(uint8_t value) {
    switch (value) {
        case static_cast<uint8_t>(Algorithm::MD5):
            return Algorithm::MD5;
        case static_cast<uint8_t>(Algorithm::SHA1):
            return Algorithm::SHA1;
        case static_cast<uint8_t>(Algorithm::SHA256):
            return Algorithm::SHA256;
        case static_cast<uint8_t>(Algorithm::SHA512):
            return Algorithm::SHA512;
        default:
            return Algorithm::UNKNOWN;
    }
}

void Hash::add_data(const std::string& data)
{
    data_buffer.insert(data_buffer.end(), data.begin(), data.end());
}

void Hash::add_data(const std::vector<uint8_t>& data)
{
    data_buffer.insert(data_buffer.end(), data.begin(), data.end());
}

std::vector<uint8_t> Hash::hash(Algorithm algorithm, bool is_hex)
{
    if (algorithm == Algorithm::UNKNOWN) {
        return std::vector<uint8_t>();
    }

    uint8_t* source = nullptr;
    switch (algorithm) {
        case Algorithm::MD5: source = md5_output; break;
        case Algorithm::SHA1: source = sha1_output; break;
        case Algorithm::SHA256: source = sha256_output; break;
        case Algorithm::SHA512: source = sha512_output; break;
        // unreachable, but we need a case for compiling
        case Algorithm::UNKNOWN: return std::vector<uint8_t>();
    }

    int length = hash_length(algorithm, is_hex);
    std::vector<uint8_t> o(length);

    if (!is_hex) {
        if (source) memcpy(o.data(), source, length);
    } else {
        for (int i = 0; i < length / 2; i++) {
            sprintf(reinterpret_cast<char*>(o.data()) + i * 2, "%02x", source[i]);
        }
    }
    return o;
}

void Hash::compute(Algorithm algorithm, bool clear_data)
{
    // Initialize hash context
    switch (algorithm)
    {
    case Algorithm::UNKNOWN:
    case Algorithm::MD5:
        // Not implemented
        break;
    case Algorithm::SHA1:
        mbedtls_sha1_init(&_sha1);
        mbedtls_sha1_starts(&_sha1);
        break;
    case Algorithm::SHA256:
        mbedtls_sha256_init(&_sha256);
        mbedtls_sha256_starts(&_sha256, 0);
        break;
    case Algorithm::SHA512:
        mbedtls_sha512_init(&_sha512);
        mbedtls_sha512_starts(&_sha512, 0);
        break;
    }

    // Update
    switch (algorithm)
    {
    case Algorithm::UNKNOWN:
    case Algorithm::MD5:
        // Not implemented
        break;
    case Algorithm::SHA1:
        mbedtls_sha1_update(&_sha1, reinterpret_cast<const unsigned char*>(data_buffer.data()), data_buffer.size());
        break;
    case Algorithm::SHA256:
        mbedtls_sha256_update(&_sha256, reinterpret_cast<const unsigned char*>(data_buffer.data()), data_buffer.size());
        break;
    case Algorithm::SHA512:
        mbedtls_sha512_update(&_sha512, reinterpret_cast<const unsigned char*>(data_buffer.data()), data_buffer.size());
        break;
    }

    // Clean up
    switch (algorithm)
    {
    case Algorithm::UNKNOWN:
    case Algorithm::MD5:
        // Not implemented
        break;
    case Algorithm::SHA1:
        memset(&sha1_output, 0, sizeof(sha1_output));
        mbedtls_sha1_finish(&_sha1, sha1_output);
        mbedtls_sha1_free(&_sha1);
        break;
    case Algorithm::SHA256:
        memset(&sha256_output, 0, sizeof(sha256_output));
        mbedtls_sha256_finish(&_sha256, sha256_output);
        mbedtls_sha256_free(&_sha256);
        break;
    case Algorithm::SHA512:
        memset(&sha512_output, 0, sizeof(sha512_output));
        mbedtls_sha512_finish(&_sha512, sha512_output);
        mbedtls_sha512_free(&_sha512);
        break;
    }

    // reset after computing if asked to
    if (clear_data) {
        init();
    }

}

uint8_t Hash::hash_length(Algorithm algorithm, bool is_hex)
{
    uint8_t len = 0;
    switch(algorithm)
    {
        case Algorithm::UNKNOWN:
            len = 0;
            break;
        case Algorithm::MD5:
            len = 16;
            break;
        case Algorithm::SHA1:
            len = 20;
            break;
        case Algorithm::SHA256:
            len = 32;
            break;
        case Algorithm::SHA512:
            len = 64;
            break;
    }

    // double the value if it's a hex string instead of plain bytes.
    if (is_hex) len <<= 1;

    return len;
}
