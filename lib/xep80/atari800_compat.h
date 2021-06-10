#ifndef ATARI800_COMPAT_H
#define ATARI800_COMPAT_H
/*
 * Jeff Piepmeier, 2021
 * FujiNet project
 * 
 * This include file has definitions to make the xep80 code borrowed from the Atari800
 * emulator package work with the FujiNet framework and firmware.
*/

#define XEP80_EMULATION

#include <stdbool.h>
#define FALSE false
#define TRUE true

#include <stdint.h>
typedef uint8_t UBYTE;
typedef uint16_t UWORD;

#endif