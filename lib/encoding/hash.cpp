#include "hash.h"

#include <sstream>
#include <iomanip>

#include <mbedtls/version.h>

// mbedtls 4.x moved the low-level hash API into TF-PSA-Crypto; only PSA remains public.
#if MBEDTLS_VERSION_MAJOR >= 4
#include <psa/crypto.h>
#else
#include <mbedtls/md.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

#if MBEDTLS_VERSION_NUMBER >= 0x03000000 || MBEDTLS_VERSION_NUMBER < 0x02070000
#define COMPAT_MBEDTLS_MD5 mbedtls_md5
#define COMPAT_MBEDTLS_SHA1 mbedtls_sha1
#define COMPAT_MBEDTLS_SHA256 mbedtls_sha256
#define COMPAT_MBEDTLS_SHA512 mbedtls_sha512
#else
#define COMPAT_MBEDTLS_MD5 mbedtls_md5_ret
#define COMPAT_MBEDTLS_SHA1 mbedtls_sha1_ret
#define COMPAT_MBEDTLS_SHA256 mbedtls_sha256_ret
#define COMPAT_MBEDTLS_SHA512 mbedtls_sha512_ret
#endif
#endif // MBEDTLS_VERSION_MAJOR >= 4

#if MBEDTLS_VERSION_MAJOR >= 4
static psa_algorithm_t to_psa_alg(Hash::Algorithm algorithm) {
    switch (algorithm) {
        case Hash::Algorithm::MD5:    return PSA_ALG_MD5;
        case Hash::Algorithm::SHA1:   return PSA_ALG_SHA_1;
        case Hash::Algorithm::SHA224: return PSA_ALG_SHA_224;
        case Hash::Algorithm::SHA256: return PSA_ALG_SHA_256;
        case Hash::Algorithm::SHA384: return PSA_ALG_SHA_384;
        case Hash::Algorithm::SHA512: return PSA_ALG_SHA_512;
        default:                      return PSA_ALG_NONE;
    }
}
#else
static mbedtls_md_type_t to_md_type(Hash::Algorithm algorithm) {
    switch (algorithm) {
        case Hash::Algorithm::MD5:    return MBEDTLS_MD_MD5;
        case Hash::Algorithm::SHA1:   return MBEDTLS_MD_SHA1;
        case Hash::Algorithm::SHA224: return MBEDTLS_MD_SHA224;
        case Hash::Algorithm::SHA256: return MBEDTLS_MD_SHA256;
        case Hash::Algorithm::SHA384: return MBEDTLS_MD_SHA384;
        case Hash::Algorithm::SHA512: return MBEDTLS_MD_SHA512;
        default:                      return MBEDTLS_MD_NONE;
    }
}
#endif

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

size_t Hash::hash_length(Algorithm algorithm, bool is_hex) {
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

int Hash::compute(Algorithm algorithm, const void *data, size_t len, uint8_t *output, size_t output_size) {
    size_t needed = hash_length(algorithm, false);
    if (needed == 0 || output_size < needed)
        return -1;

#if MBEDTLS_VERSION_MAJOR >= 4
    size_t out_len = 0;
    psa_crypto_init();
    psa_status_t status = psa_hash_compute(to_psa_alg(algorithm), (const uint8_t *)data, len, output, output_size, &out_len);
    return (status == PSA_SUCCESS && out_len == needed) ? 0 : -1;
#else
    const unsigned char *in = (const unsigned char *)data;
    switch (algorithm) {
        case Algorithm::MD5:
            COMPAT_MBEDTLS_MD5(in, len, output);
            break;
        case Algorithm::SHA1:
            COMPAT_MBEDTLS_SHA1(in, len, output);
            break;
        case Algorithm::SHA224:
            COMPAT_MBEDTLS_SHA256(in, len, output, 1);
            break;
        case Algorithm::SHA256:
            COMPAT_MBEDTLS_SHA256(in, len, output, 0);
            break;
        case Algorithm::SHA384:
            COMPAT_MBEDTLS_SHA512(in, len, output, 1);
            break;
        case Algorithm::SHA512:
            COMPAT_MBEDTLS_SHA512(in, len, output, 0);
            break;
        default:
            return -1;
    }
    return 0;
#endif
}

void Hash::compute_md5() {
    if (!key.empty()) {
        compute_hmac(Algorithm::MD5, 16);
        return;
    }

    hash_output.resize(16);
    compute(Algorithm::MD5, accumulated_data.data(), accumulated_data.size(), hash_output.data(), hash_output.size());
}

void Hash::compute_sha1() {
    if (!key.empty()) {
        compute_hmac(Algorithm::SHA1, 20);
        return;
    }

    hash_output.resize(20);
    compute(Algorithm::SHA1, accumulated_data.data(), accumulated_data.size(), hash_output.data(), hash_output.size());
}

void Hash::compute_sha256(int is224) {
    Algorithm algorithm = is224 ? Algorithm::SHA224 : Algorithm::SHA256;
    if (!key.empty()) {
        compute_hmac(algorithm, is224 ? 28 : 32);
        return;
    }

    hash_output.resize(is224 ? 28 : 32);
    compute(algorithm, accumulated_data.data(), accumulated_data.size(), hash_output.data(), hash_output.size());
}

void Hash::compute_sha512(int is384) {
    Algorithm algorithm = is384 ? Algorithm::SHA384 : Algorithm::SHA512;
    if (!key.empty()) {
        compute_hmac(algorithm, is384 ? 48 : 64);
        return;
    }

    hash_output.resize(is384 ? 48 : 64);
    compute(algorithm, accumulated_data.data(), accumulated_data.size(), hash_output.data(), hash_output.size());
}

void Hash::compute_hmac(Algorithm algorithm, uint8_t size) {
    hash_output.resize(size);
#if MBEDTLS_VERSION_MAJOR >= 4
    psa_crypto_init();
    psa_algorithm_t alg = PSA_ALG_HMAC(to_psa_alg(algorithm));
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, alg);
    psa_key_id_t key_id = 0;
    if (psa_import_key(&attributes, (const uint8_t *)key.data(), key.size(), &key_id) != PSA_SUCCESS)
        return;
    size_t out_len = 0;
    psa_mac_compute(key_id, alg, accumulated_data.data(), accumulated_data.size(), hash_output.data(), hash_output.size(), &out_len);
    psa_destroy_key(key_id);
#else
    mbedtls_md_hmac(
        mbedtls_md_info_from_type(to_md_type(algorithm)),
        (const unsigned char *)key.data(), key.size(),
        (const unsigned char *)accumulated_data.data(), accumulated_data.size(),
        hash_output.data()
    );
#endif
}

std::string Hash::bytes_to_hex(const std::vector<uint8_t>& bytes) const {
    std::stringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    for (auto byte : bytes) {
        hex_stream << std::setw(2) << static_cast<int>(byte);
    }
    return hex_stream.str();
}
