#ifndef HASH_H
#define HASH_H

#include <vector>
#include <string>

#include <mbedtls/md.h>

class Hash {
public:
    enum class Algorithm {
        UNKNOWN = -1, MD5, SHA1, SHA224, SHA256, SHA384, SHA512
    };

    Hash();
    ~Hash();

    std::string key = "";
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

    void compute_md5();
    void compute_sha1();
    void compute_sha256(int is224 = 0);
    void compute_sha512(int is384 = 0);
    void compute_md(mbedtls_md_type_t md_type, uint8_t size);
    std::string bytes_to_hex(const std::vector<uint8_t>& bytes) const;
};

extern Hash hasher;

#endif // HASH_H