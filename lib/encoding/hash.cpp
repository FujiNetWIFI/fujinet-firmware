#include <memory>
#include <string>
#include <cstring>

#include "hash.h"

Hash hasher;

std::vector<uint8_t> Hash::hash_output(uint16_t m, char hash_mode, uint16_t& olen)
{
    std::vector<uint8_t> o(129, 0);

    switch (hash_mode)
    {
    case 0: // MD5
        olen = 16;

        if (m == 0)
            memcpy(o.data(), md5_output, 16);
        else if (m == 1)
        {
            olen <<= 1;
            for(int i = 0; i < 16; i++)
                sprintf((char *)o.data() + i*2, "%02x", md5_output[i]);
        }
        break;
    case 1: // SHA1
        olen = 20;

        if (m == 0)
            memcpy(o.data(), sha1_output, 20);
        else if (m == 1)
        {
            olen <<= 1;
            for(int i = 0; i < 20; i++)
                sprintf((char *)o.data() + i*2, "%02x", sha1_output[i]);
        }
        break;
    case 2: // SHA256
        olen = 32;

        if (m == 0)
            memcpy(o.data(), sha256_output, 32);
        else if (m == 1)
        {
            olen <<= 1;
            for(int i = 0; i < 32; i++)
                sprintf((char *)o.data() + i*2, "%02x", sha256_output[i]);
        }
        break;
    case 3: // SHA512
        olen = 64;

        if (m == 0)
            memcpy(o.data(), sha512_output, 64);
        else if (m == 1)
        {
            olen <<= 1;
            for(int i = 0; i < 64; i++)
                sprintf((char *)o.data() + i*2, "%02x", sha512_output[i]);
        }
        break;
    }

    return o;
}

void Hash::compute(uint16_t m, const std::string& data)
{

    // Initialize hash context
    switch (m)
    {
    case 0: // md5
        // Not implemented
        break;
    case 1: // sha1
        mbedtls_sha1_init(&_sha1);
        mbedtls_sha1_starts(&_sha1);
        break;
    case 2: // sha256
        mbedtls_sha256_init(&_sha256);
        mbedtls_sha256_starts(&_sha256, 0);
        break;
    case 3: // sha512
        mbedtls_sha512_init(&_sha512);
        mbedtls_sha512_starts(&_sha512, 0);
        break;
    }

    // Update
    switch (m)
    {
    case 0: // MD5
        // Not implemented
        break;
    case 1: // SHA1
        mbedtls_sha1_update(&_sha1, reinterpret_cast<const unsigned char*>(data.data()), data.size());
        break;
    case 2: // SHA256
        mbedtls_sha256_update(&_sha256, reinterpret_cast<const unsigned char*>(data.data()), data.size());
        break;
    case 3: // SHA512
        mbedtls_sha512_update(&_sha512, reinterpret_cast<const unsigned char*>(data.data()), data.size());
        break;
    }

    // Clean up
    switch (m)
    {
    case 0: // MD5
        // Not implemented
        break;
    case 1: // SHA1
        mbedtls_sha1_finish(&_sha1, sha1_output);
        mbedtls_sha1_free(&_sha1);
        break;
    case 2: // SHA256
        mbedtls_sha256_finish(&_sha256, sha256_output);
        mbedtls_sha256_free(&_sha256);
        break;
    case 3: // SHA512
        mbedtls_sha512_finish(&_sha512, sha512_output);
        mbedtls_sha512_free(&_sha512);
        break;
    }
}