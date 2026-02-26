#include "hash.h"

#include <sstream>
#include <iomanip>
#include <mbedtls/md.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

// TODO: Add support for other algorithms and hardware acceleration

Hash hasher;

Hash::Hash() {}

Hash::~Hash() {
    clear();
}

Hash::Algorithm Hash::to_algorithm(uint8_t value) {
    switch (value) {
        case static_cast<uint8_t>(Algorithm::MD5):
            return Hash::Algorithm::MD5;
        case static_cast<uint8_t>(Algorithm::SHA1):
            return Hash::Algorithm::SHA1;
        case static_cast<uint8_t>(Algorithm::SHA256):
            return Hash::Algorithm::SHA256;
        case static_cast<uint8_t>(Algorithm::SHA512):
            return Hash::Algorithm::SHA512;
        default:
            return Hash::Algorithm::UNKNOWN;
    }
}

Hash::Algorithm Hash::from_string(std::string hash_name)
{
    if (hash_name == "MD5") {
        return Hash::Algorithm::MD5;
    } else if (hash_name == "SHA1") {
        return Hash::Algorithm::SHA1;
    } else if (hash_name == "SHA224") {
        return Hash::Algorithm::SHA224;
    } else if (hash_name == "SHA256") {
        return Hash::Algorithm::SHA256;
    } else if (hash_name == "SHA384") {
        return Hash::Algorithm::SHA384;
    } else if (hash_name == "SHA512") {
        return Hash::Algorithm::SHA512;
    } else {
        return Hash::Algorithm::UNKNOWN;
    }
}

void Hash::add_data(const std::vector<uint8_t>& data) {
    accumulated_data.insert(accumulated_data.end(), data.begin(), data.end());
}

void Hash::add_data(const std::string& data) {
    accumulated_data.insert(accumulated_data.end(), data.begin(), data.end());
}

void Hash::clear() {
    accumulated_data.clear();
    accumulated_data.shrink_to_fit();
}

size_t Hash::hash_length(Algorithm algorithm, bool is_hex) const {
    size_t length = 0;
    switch (algorithm) {
        case Algorithm::MD5:
            length = 16;
            break;
        case Algorithm::SHA1:
            length = 20;
            break;
        case Algorithm::SHA224:
            length = 28;
            break;
        case Algorithm::SHA256:
            length = 32;
            break;
        case Algorithm::SHA384:
            length = 48;
            break;
        case Algorithm::SHA512:
            length = 64;
            break;
        default:
            return 0;
    }
    return is_hex ? length * 2 : length;
}

void Hash::compute(Algorithm algorithm, bool clear_data) {
    hash_output.clear();
    switch (algorithm) {
        case Algorithm::MD5:
            compute_md5();
            break;
        case Algorithm::SHA1:
            compute_sha1();
            break;
        case Algorithm::SHA224:
            compute_sha256(1);
            break;
        case Algorithm::SHA256:
            compute_sha256();
            break;
        case Algorithm::SHA384:
            compute_sha512(1);
            break;
        case Algorithm::SHA512:
            compute_sha512();
            break;
        default:
            break;
    }
    if (clear_data) {
        clear();
    }
    //printf("hash[%s]\n", output_hex().c_str());
}

std::vector<uint8_t> Hash::output_binary() const {
    return hash_output;
}

std::string Hash::output_hex() const {
    return bytes_to_hex(hash_output);
}

void Hash::compute_md5() {
    //printf("md5\n");
    if (!key.empty()) {
        compute_md(MBEDTLS_MD_MD5, 16);
        return;
    }

    hash_output.resize(16);
    mbedtls_md5((const unsigned char *)accumulated_data.data(), accumulated_data.size(), hash_output.data());

    // mbedtls_md5_context ctx;
    // mbedtls_md5_init(&ctx);
    // mbedtls_md5_starts(&ctx);
    // mbedtls_md5_update(&ctx, accumulated_data.data(), accumulated_data.size());
    // hash_output.resize(16);
    // mbedtls_md5_finish(&ctx, hash_output.data());
    // mbedtls_md5_free(&ctx);
}

void Hash::compute_sha1() {
    //printf("sha1\n");
    if (!key.empty()) {
        compute_md(MBEDTLS_MD_SHA1, 20);
        return;
    }

    hash_output.resize(20);
    mbedtls_sha1((const unsigned char *)accumulated_data.data(), accumulated_data.size(), hash_output.data());

    // mbedtls_sha1_context ctx;
    // mbedtls_sha1_init(&ctx);
    // mbedtls_sha1_starts(&ctx);
    // mbedtls_sha1_update(&ctx, accumulated_data.data(), accumulated_data.size());
    // hash_output.resize(20);
    // mbedtls_sha1_finish(&ctx, hash_output.data());
    // mbedtls_sha1_free(&ctx);
}

void Hash::compute_sha256(int is224) {
    //printf("sha256\n");
    if (!key.empty()) {
        if (is224)
            compute_md(MBEDTLS_MD_SHA224, 28);
        else
            compute_md(MBEDTLS_MD_SHA256, 32);
        return;
    }

    if (is224)
        hash_output.resize(28);
    else
        hash_output.resize(32);
    mbedtls_sha256((const unsigned char *)accumulated_data.data(), accumulated_data.size(), hash_output.data(), is224);

    // mbedtls_sha256_context ctx;
    // mbedtls_sha256_init(&ctx);
    // mbedtls_sha256_starts(&ctx, 0);
    // mbedtls_sha256_update(&ctx, accumulated_data.data(), accumulated_data.size());
    // hash_output.resize(32);
    // mbedtls_sha256_finish(&ctx, hash_output.data());
    // mbedtls_sha256_free(&ctx);
}

void Hash::compute_sha512(int is384) {
    //printf("sha512\n");
    if (!key.empty()) {
        if (is384)
            compute_md(MBEDTLS_MD_SHA384, 48);
        else
            compute_md(MBEDTLS_MD_SHA512, 64);
        return;
    }

    if (is384)
        hash_output.resize(48);
    else
        hash_output.resize(64);
    mbedtls_sha512((const unsigned char *)accumulated_data.data(), accumulated_data.size(), hash_output.data(), is384);

    // mbedtls_sha512_context ctx;
    // mbedtls_sha512_init(&ctx);
    // mbedtls_sha512_starts(&ctx, 0);
    // mbedtls_sha512_update(&ctx, accumulated_data.data(), accumulated_data.size());
    // hash_output.resize(64);
    // mbedtls_sha512_finish(&ctx, hash_output.data());
    // mbedtls_sha512_free(&ctx);
}

void Hash::compute_md(mbedtls_md_type_t md_type, uint8_t size) {
    //printf("hmac\n");

    hash_output.resize(size);
    mbedtls_md_hmac(
        mbedtls_md_info_from_type(md_type), 
        (const unsigned char *)key.data(), key.size(), 
        (const unsigned char *)accumulated_data.data(), accumulated_data.size(), 
        hash_output.data()
    );

    // mbedtls_md_context_t ctx;
    // mbedtls_md_init(&ctx);
    // mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    // mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.data(), key.size());
    // mbedtls_md_hmac_update(&ctx, accumulated_data.data(), accumulated_data.size());
    // hash_output.resize(size);
    // mbedtls_md_hmac_finish(&ctx, hash_output.data());
    // mbedtls_md_free(&ctx);
}

std::string Hash::bytes_to_hex(const std::vector<uint8_t>& bytes) const {
    std::stringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    for (auto byte : bytes) {
        hex_stream << std::setw(2) << static_cast<int>(byte);
    }
    return hex_stream.str();
}
