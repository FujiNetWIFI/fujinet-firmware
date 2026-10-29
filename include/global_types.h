/**
 * Global types used throughout this #FujiNet code
 * Intended to increase readability.
 */
#ifndef GLOBAL_TYPES_H
#define GLOBAL_TYPES_H

#include "fuji_endian.h"
#include <cstdint>
#include <cstring>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

using ByteBuffer = std::vector<std::uint8_t>;

// Obsolete, do not use
typedef uint8_t* Buffer;
typedef uint16_t BufferLength;
typedef uint16_t AuxWord;

typedef enum class FUJI_ERROR {
    NONE = 0,
    UNSPECIFIED = 1,
} fujiError_t;

struct success_is_true {
    bool value;
    explicit success_is_true(bool v) : value(v) {}
    operator bool() const { return value; }
    bool is_success() const { return value; }
    bool is_error()   const { return !value; }
};
#define RETURN_SUCCESS_AS_TRUE() return success_is_true(true)
#define RETURN_ERROR_AS_FALSE()  return success_is_true(false)
#define RETURN_SUCCESS_IF(expr)  return success_is_true(expr)

struct error_is_true {
    bool value;
    explicit error_is_true(bool v) : value(v) {}
    operator bool() const { return value; }
    bool is_success() const { return !value; }
    bool is_error()   const { return value; }
};
#define RETURN_ERROR_AS_TRUE()    return error_is_true(true)
#define RETURN_SUCCESS_AS_FALSE() return error_is_true(false)
#define RETURN_ERROR_IF(expr)     return error_is_true(expr)

// A file size, -1 on error. It reads as an ssize_t; an unsigned or narrower type takes it only
// through to(), which refuses -1 and a size that doesn't fit.
class file_size_t
{
    ssize_t _size;

    template <typename T>
    static constexpr bool narrows = std::is_arithmetic_v<T> && !std::is_same_v<T, ssize_t>
        && (std::is_unsigned_v<T> || std::is_floating_point_v<T> || sizeof(T) < sizeof(ssize_t));

public:
    constexpr explicit file_size_t(ssize_t size) : _size(size) {}

    constexpr operator ssize_t() const { return _size; }
    template <typename T, std::enable_if_t<narrows<T>, int> = 0>
    operator T() const = delete;

    template <typename T>
    success_is_true to(T *out) const
    {
        if (_size < 0 || !std::in_range<T>(_size))
            RETURN_ERROR_AS_FALSE();
        *out = static_cast<T>(_size);
        RETURN_SUCCESS_AS_TRUE();
    }

    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend constexpr bool operator==(file_size_t a, T b) { return std::cmp_equal(a._size, b); }
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend constexpr bool operator!=(file_size_t a, T b) { return std::cmp_not_equal(a._size, b); }
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend constexpr bool operator<(file_size_t a, T b) { return std::cmp_less(a._size, b); }
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend constexpr bool operator<=(file_size_t a, T b) { return std::cmp_less_equal(a._size, b); }
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend constexpr bool operator>(file_size_t a, T b) { return std::cmp_greater(a._size, b); }
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend constexpr bool operator>=(file_size_t a, T b) { return std::cmp_greater_equal(a._size, b); }
    // Arithmetic would carry -1 along as a size
    template <typename T> friend ssize_t operator+(file_size_t, T) = delete;
    template <typename T> friend ssize_t operator+(T, file_size_t) = delete;
    template <typename T> friend ssize_t operator-(file_size_t, T) = delete;
    template <typename T> friend ssize_t operator-(T, file_size_t) = delete;
};

struct [[nodiscard]] size_result_t {
    size_result_t() = delete;
    size_result_t(ssize_t v) : _value(v) {}   // implicit: `return n;` works too

    bool is_success() const { return _value >= 0; }
    bool is_error()   const { return _value <  0; }

    operator ssize_t() const { return _value; }

    // Blocks `if (x)`, `!x`, `x && y`, `x || y`, `x ? a : b`.
    // Overload resolution prefers this exact match over the ssize_t path,
    // so the deleted function is chosen and the compile fails.
    explicit operator bool() const = delete;

private:
    ssize_t _value;
};
#define RETURN_SIZE(n)       return size_result_t(static_cast<ssize_t>(n))
#define RETURN_SIZE_ERROR()  return size_result_t(-1)

struct u16le_t {
  uint8_t bytes[2];
  operator uint16_t() const { uint16_t v; std::memcpy(&v, bytes, 2); return le16toh(v); }
  u16le_t& operator=(uint16_t v) {
    uint16_t out = htole16(v); std::memcpy(bytes, &out, 2);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u16le_t) == 2, "u16le_t must be 2 bytes");

struct u24le_t {
  uint8_t bytes[3];
  operator uint32_t() const { return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16); }
  u24le_t& operator=(uint32_t v) {
    bytes[0] = v; bytes[1] = v >> 8; bytes[2] = v >> 16;
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u24le_t) == 3, "u24le_t must be 3 bytes");

struct u32le_t {
  uint8_t bytes[4];
  operator uint32_t() const { uint32_t v; std::memcpy(&v, bytes, 4); return le32toh(v); }
  u32le_t& operator=(uint32_t v) {
    uint32_t out = htole32(v); std::memcpy(bytes, &out, 4);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u32le_t) == 4, "u32le_t must be 4 bytes");

struct u16be_t {
  uint8_t bytes[2];
  operator uint16_t() const { uint16_t v; std::memcpy(&v, bytes, 2); return be16toh(v); }
  u16be_t& operator=(uint16_t v) {
    uint16_t out = htobe16(v); std::memcpy(bytes, &out, 2);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u16be_t) == 2, "u16be_t must be 2 bytes");

struct u24be_t {
  uint8_t bytes[3];
  operator uint32_t() const { return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2]; }
  u24be_t& operator=(uint32_t v) {
    bytes[0] = v >> 16; bytes[1] = v >> 8; bytes[2] = v;
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u24be_t) == 3, "u24be_t must be 3 bytes");

struct u32be_t {
  uint8_t bytes[4];
  operator uint32_t() const { uint32_t v; std::memcpy(&v, bytes, 4); return be32toh(v); }
  u32be_t& operator=(uint32_t v) {
    uint32_t out = htobe32(v); std::memcpy(bytes, &out, 4);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u32be_t) == 4, "u32be_t must be 4 bytes");

// unsigned types in native endianness
#ifdef BUILD_COCO
using u16ne_t = u16be_t;
using u24ne_t = u24be_t;
using u32ne_t = u32be_t;
#else
using u16ne_t = u16le_t;
using u24ne_t = u24le_t;
using u32ne_t = u32le_t;
#endif /* BUILD_COCO */

#endif
