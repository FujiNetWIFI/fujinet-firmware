/**
 * Global types used throughout this #FujiNet code
 * Intended to increase readability.
 */
#ifndef GLOBAL_TYPES_H
#define GLOBAL_TYPES_H

#include <stdint.h>

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

#endif
