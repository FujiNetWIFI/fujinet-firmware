/**
 * Global types used throughout this #FujiNet code
 * Intended to increase readability.
 */
#ifndef GLOBAL_TYPES_H
#define GLOBAL_TYPES_H

#include "fuji_endian.h"
#include <cstdint>
#include <cstring>

/**
 * byte array for buffers
 */
typedef uint8_t* Buffer;

/**
 * Used for specifying buffer lengths
 */
typedef uint16_t BufferLength;

/**
 * Used to specify both aux1/aux2 values
 */
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

struct u16le_t {
  uint8_t bytes[2];
  //u16le_t(uint16_t v = 0) { *this = v; }
  operator uint16_t() const { uint16_t v; std::memcpy(&v, bytes, 2); return le16toh(v); }
  u16le_t& operator=(uint16_t v) {
    uint16_t out = htole16(v); std::memcpy(bytes, &out, 2);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u16le_t) == 2, "u16le_t must be 2 bytes");

struct u24le_t {
  uint8_t bytes[3];
  //u24le_t(uint32_t v = 0) { *this = v; }
  operator uint32_t() const { return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16); }
  u24le_t& operator=(uint32_t v) {
    bytes[0] = v; bytes[1] = v >> 8; bytes[2] = v >> 16;
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u24le_t) == 3, "u24le_t must be 3 bytes");

struct u32le_t {
  uint8_t bytes[4];
  //u32le_t(uint32_t v = 0) { *this = v; }
  operator uint32_t() const { uint32_t v; std::memcpy(&v, bytes, 4); return le32toh(v); }
  u32le_t& operator=(uint32_t v) {
    uint32_t out = htole32(v); std::memcpy(bytes, &out, 4);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u32le_t) == 4, "u32le_t must be 4 bytes");

struct u16be_t {
  uint8_t bytes[2];
  //u16be_t(uint16_t v = 0) { *this = v; }
  operator uint16_t() const { uint16_t v; std::memcpy(&v, bytes, 2); return be16toh(v); }
  u16be_t& operator=(uint16_t v) {
    uint16_t out = htobe16(v); std::memcpy(bytes, &out, 2);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u16be_t) == 2, "u16be_t must be 2 bytes");

struct u24be_t {
  uint8_t bytes[3];
  //u24be_t(uint32_t v = 0) { *this = v; }
  operator uint32_t() const { return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2]; }
  u24be_t& operator=(uint32_t v) {
    bytes[0] = v >> 16; bytes[1] = v >> 8; bytes[2] = v;
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u24be_t) == 3, "u24be_t must be 3 bytes");

struct u32be_t {
  uint8_t bytes[4];
  //u32be_t(uint32_t v = 0) { *this = v; }
  operator uint32_t() const { uint32_t v; std::memcpy(&v, bytes, 4); return be32toh(v); }
  u32be_t& operator=(uint32_t v) {
    uint32_t out = htobe32(v); std::memcpy(bytes, &out, 4);
    return *this;
  }
} __attribute__((packed));
static_assert(sizeof(u32be_t) == 4, "u32be_t must be 4 bytes");

#endif
