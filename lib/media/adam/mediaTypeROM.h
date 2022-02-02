#ifndef _MEDIATYPE_ROM_
#define _MEDIATYPE_ROM_

#include <stdio.h>

#include "mediaType.h"


class MediaTypeROM : public MediaType
{
private:
    // Block 0 and 1 data borrowed from Sean Myers' AdamNet Drive Emulator
    // https://github.com/Kalidomra/AdamNet-Drive-Emulator/blob/master/AdamNet_Drive_Emulator/SDLoadBlock.ino#L95

    const char block0[1024]={0x3A,0x6F,0xFD,0x01,0x00,0x00,0x11,0x01,    // This is the boot block it just loads block 1 into 0x2000 and then jumps to 0x2000
                             0x00,0x21,0x00,0x20,0xCD,0xF3,0xFC,0xC3,
                             0x00,0x20};
    const char block1[1024]={0x11,0x02,0x00,0x21,0x00,0x40,0x3A,0x6F,  // This code will load the rom file from block 2 and above into 0x4000
                             0xFD,0x01,0x00,0x00,0xCD,0xF3,0xFC,0x3E,  // Then the PCB's are move to 0x2100 and the AdamNet scanned
                             0x21,0xBB,0xCA,0x1D,0x20,0x01,0x00,0x04,  // (Thanks to Milli for this information)
                             0x09,0x13,0xC3,0x06,0x20,0x21,0x00,0x21,  // It will then bank switch in OS7 and move the rom code to 0x8000.
                             0xCD,0x7B,0xFC,0xCD,0x8A,0xFC,0x3E,0x03,  // finally it jumps to 0x0000 to start execution of the CV rom.
                             0xCD,0x14,0xFD,0x21,0xFF,0xBF,0x11,0xFF,
                             0xFF,0x01,0x00,0x80,0xED,0xB8,0xC3,0x00,
                             0x00};
    char rom[32768];
public:
    virtual bool read(uint32_t blockNum, uint16_t *readcount) override;
    virtual bool write(uint32_t blockNum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;

    virtual uint8_t status() override;

    static bool create(FILE *f, uint32_t numBlock);
};


#endif // _DISKTYPE_ROM_