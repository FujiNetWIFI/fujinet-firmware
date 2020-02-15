/**
 * Routine for writing Mr. Robot VTOC
 * essentially blocks off sectors 0-214
 */

#ifndef VTOC_H
#define VTOC_H

typedef struct _vtoc {
  unsigned char dos_code;
  unsigned short total_sectors;
  unsigned short available_sectors;
  unsigned char __unused1[5];
  unsigned char bitmap[90]; // 720 sector bitmap
  unsigned char __unused2[28];
} VTOC;

void vtoc_write(unsigned char drive_num);

#endif /* VTOC_H */
