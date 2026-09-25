/*
 * sit_crc16.h - CRC-16/ARC, used for the classic-format 112-byte entry
 * header checksum and for verifying decompressed data-fork content
 * (skipped for method 15/Arsenic, whose stored CRC field is stale in
 * the reference implementation - see sit_arsenic.c).
 *
 * Derived by hand from XADMaster's XADCRC/XADCRCTable_a001 table
 * generation logic (table[1] == 0xC0C1, the known CRC-16/ARC constant):
 * init 0, poly 0xA001 (reflected 0x8005), no final XOR, LSB-first.
 */
#ifndef FN_SIT_CRC16_H
#define FN_SIT_CRC16_H

#include <stdint.h>
#include <stddef.h>

uint16_t sit_crc16_update(uint16_t crc, const uint8_t *buf, size_t n);

#endif /* FN_SIT_CRC16_H */
