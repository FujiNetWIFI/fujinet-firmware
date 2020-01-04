/**
 * ATX Data Structures
 * Based on work by Daniel Noguerol
 */

#ifndef ATX_H
#define ATX_H

#define ATX_VERSION    0x01
#define STS_EXTENDED  0x40
#define MAX_TRACK 42

#define AU_FULL_ROTATION         26042
#define AU_ONE_SECTOR_READ       1208

#define MASK_FDC_DLOST           0x04
#define MASK_FDC_MISSING         0x10
#define MASK_EXTENDED_DATA       0x40

typedef struct atxFileHeader {
    unsigned char signature[4];
    unsigned short version;
    unsigned short minVersion;
    unsigned short creator;
    unsigned short creatorVersion;
    unsigned long flags;
    unsigned short imageType;
    unsigned char density;
    unsigned char reserved0;
    unsigned long imageId;
    unsigned short imageVersion;
    unsigned short reserved1;
    unsigned long startData;
    unsigned long endData;
    unsigned char reserved2[12];
} ATXFileHeader;

typedef struct atxTrackHeader {
    unsigned long size;
    unsigned short type;
    unsigned short reserved0;
    unsigned char trackNumber;
    unsigned char reserved1;
    unsigned short sectorCount;
    unsigned short rate;
    unsigned short reserved3;
    unsigned long flags;
    unsigned long headerSize;
    unsigned char reserved4[8];
} ATXTrackHeader;

typedef struct atxSectorListHeader {
    unsigned long next;
    unsigned short type;
    unsigned short pad0;
} ATXSectorListHeader;

typedef struct atxSectorHeader {
    unsigned char number;
    unsigned char status;
    unsigned short timev;
    unsigned long data;
} ATXSectorHeader;

typedef struct atxTrackChunk {
    unsigned long size;
    unsigned char type;
    unsigned char sectorIndex;
    unsigned short data;
} ATXTrackChunk;

typedef struct atxTrackInfo {
  unsigned long offset;  
} ATXTrackInfo;

#endif /* ATX_H */
