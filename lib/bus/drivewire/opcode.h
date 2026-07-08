#ifndef OPCODE_H
#define OPCODE_H

#include <cstdint>

/* Operation Codes */
typedef enum class OP : uint8_t {
    NOP         = 0,
    JEFF        = 0xA5,
    SERREAD     = 'C',
    SERREADM    = 'c',
    SERWRITE    = 0xC3,
    SERWRITEM   = 0x64,
    GETSTAT     = 'G',
    SETSTAT     = 'S',
    SERGETSTAT  = 'D',
    SERSETSTAT  = 'D'+128,
    READ        = 'R',
    READEX      = 'R'+128,
    WRITE       = 'W',
    REREAD      = 'r',
    REREADEX    = 'r'+128,
    REWRITE     = 'w',
    INIT        = 'I',
    SERINIT     = 'E',
    SERTERM     = 'E'+128,
    DWINIT      = 'Z',
    TERM        = 'T',
    TIME        = '#',
    RESET3      = 0xF8,
    RESET2      = 0xFE,
    RESET1      = 0xFF,
    PRINT       = 'P',
    PRINTFLUSH  = 'F',
    VPORT_READ  = 'C',
    FUJI        = 0xE2,
    NET         = 0xE3,
    CPM         = 0xE4,
    CLOCK       = 0xE5,
    NAMEOBJ_MNT = 0x01,
    FASTWRITE_0 = 0x80,
    FASTWRITE_F = 0x8F,
} dwOpcode_t;

#endif /* OPCODE_H */
