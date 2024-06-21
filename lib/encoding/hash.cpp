#include <sstream>
#include <iomanip>

#include "hash.h"

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
    } else if (hash_name == "SHA256") {
        return Hash::Algorithm::SHA256;
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
        case Algorithm::SHA256:
            length = 32;
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
        case Algorithm::SHA1:
            compute_sha1();
            break;
        case Algorithm::SHA256:
            compute_sha256();
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
}

std::vector<uint8_t> Hash::output_binary() const {
    return hash_output;
}

std::string Hash::output_hex() const {
    return bytes_to_hex(hash_output);
}

void Hash::compute_sha1() {
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, accumulated_data.data(), accumulated_data.size());
    hash_output.resize(20);
    mbedtls_sha1_finish(&ctx, hash_output.data());
    mbedtls_sha1_free(&ctx);
}

void Hash::compute_sha256() {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, accumulated_data.data(), accumulated_data.size());
    hash_output.resize(32);
    mbedtls_sha256_finish(&ctx, hash_output.data());
    mbedtls_sha256_free(&ctx);
}

void Hash::compute_sha512() {
    mbedtls_sha512_context ctx;
    mbedtls_sha512_init(&ctx);
    mbedtls_sha512_starts(&ctx, 0);
    mbedtls_sha512_update(&ctx, accumulated_data.data(), accumulated_data.size());
    hash_output.resize(64);
    mbedtls_sha512_finish(&ctx, hash_output.data());
    mbedtls_sha512_free(&ctx);
}

std::string Hash::bytes_to_hex(const std::vector<uint8_t>& bytes) const {
    std::stringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    for (auto byte : bytes) {
        hex_stream << std::setw(2) << static_cast<int>(byte);
    }
    return hex_stream.str();
}