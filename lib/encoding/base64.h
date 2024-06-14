/*
 * Base64 encoding/decoding (RFC1341)
 *
 * Converted to modern C++ by Mark Fisher
 *
 */

#ifndef BASE64_H
#define BASE64_H

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>

class Base64 {
private:
    static inline const char base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static inline const char base64_url_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    static std::unique_ptr<char[]> base64_gen_encode(const unsigned char* src, size_t len, size_t* out_len, const char* table, int add_pad);
    static std::unique_ptr<unsigned char[]> base64_gen_decode(const char* src, size_t len, size_t* out_len, const char* table);

public:
    /**
     * base64_encode - Base64 encode
     * @src: Data to be encoded
     * @len: Length of the data to be encoded
     * @out_len: Pointer to output length variable, or %NULL if not used
     * Returns: Allocated buffer of out_len bytes of encoded data,
     * or nullptr on failure
     *
     * Returned buffer is
     * nul terminated to make it easier to use as a C string. The nul terminator is
     * not included in out_len.
     */
    static std::unique_ptr<char[]> encode(const void* src, size_t len, size_t* out_len);
    static std::unique_ptr<char[]> url_encode(const void* src, size_t len, size_t* out_len);

    /**
     * base64_decode - Base64 decode
     * @src: Data to be decoded
     * @len: Length of the data to be decoded
     * @out_len: Pointer to output length variable
     * Returns: Allocated buffer of out_len bytes of decoded data,
     * or nullptr on failure
     *
     */
    static std::unique_ptr<unsigned char[]> decode(const char* src, size_t len, size_t* out_len);
    static std::unique_ptr<unsigned char[]> url_decode(const char* src, size_t len, size_t* out_len);

    std::string get_buffer() const { return base64_buffer; }
    void set_buffer(const std::string& buffer) { base64_buffer = buffer; }
    void clear_buffer() { base64_buffer.clear(); }
    void add_buffer(const std::string& extra) { base64_buffer += extra; }

    std::string base64_buffer;

};

extern Base64 base64;

#endif /* BASE64_H */