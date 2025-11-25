#ifndef HASH_H
#define HASH_H

#include <vector>
#include <string>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR < 4
#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#endif /* MBEDTLS_VERSION_MAJOR < 4 */

class Hash {
public:
    enum class Algorithm {
        UNKNOWN = -1, MD5, SHA1, SHA256, SHA512
    };

    Hash();
    ~Hash();

    void add_data(const std::vector<uint8_t>& data);
    void add_data(const std::string& data);
    void clear();
    size_t hash_length(Algorithm algorithm, bool is_hex) const;
    void compute(Algorithm algorithm, bool clear_data);
    std::vector<uint8_t> output_binary() const;
    std::string output_hex() const;

    static Hash::Algorithm to_algorithm(uint8_t value);
    static Hash::Algorithm from_string(std::string hash_name);

private:
    std::vector<uint8_t> accumulated_data;
    std::vector<uint8_t> hash_output;

    void compute_sha1();
    void compute_sha256();
    void compute_sha512();
    std::string bytes_to_hex(const std::vector<uint8_t>& bytes) const;
};

extern Hash hasher;

#endif // HASH_H
