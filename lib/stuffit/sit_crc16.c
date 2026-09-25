/*
 * sit_crc16.c - see sit_crc16.h.
 */
#include "sit_crc16.h"

uint16_t sit_crc16_update(uint16_t crc, const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc = (uint16_t)(crc ^ buf[i]);
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0xA001);
            else crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
