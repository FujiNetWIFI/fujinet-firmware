#ifndef USE_ENDIAN_H
//#warning Use endian.h
// Retruns a uint16 value given two bytes in high-low order
#define UINT16_FROM_HILOBYTES(high, low) ((uint16_t)high << 8 | low)

// Returns a uint16 value from the little-endian version
#define UINT16_FROM_LE_UINT16(_ui16) \
    (_ui16 << 8 | _ui16 >> 8)
// Returns a uint32 value from the little-endian version
#define UINT32_FROM_LE_UINT32(_ui32) \
    ((_ui32 >> 24 & 0x000000FF) | (_ui32 >> 8 & 0x0000FF00) | (_ui32 << 8 & 0x00FF0000) | (_ui32 << 24 & 0xFF000000))

// Returns the high byte (MSB) of a uint16 value
#define HIBYTE_FROM_UINT16(value) ((uint8_t)((value >> 8) & 0xFF))
// Returns the low byte (LSB) of a uint16 value
#define LOBYTE_FROM_UINT16(value) ((uint8_t)(value & 0xFF))
#endif /* USE_ENDIAN_H */
