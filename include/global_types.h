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

#endif