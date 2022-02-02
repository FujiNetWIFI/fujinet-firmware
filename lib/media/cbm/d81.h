// .D81 - The D81 disk image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC354
// https://ist.uwaterloo.ca/~schepers/formats/D81.TXT
//


#ifndef MEDIA_CBM_D81
#define MEDIA_CBM_D81

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D81IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D81IStream(std::shared_ptr<MIStream> is) : D64IStream(is) 
    {
        // D81 Offsets
        directory_header_offset = {40, 0, 0x04};
        directory_list_offset = {40, 3, 0x00};
        block_allocation_map = { {40, 1, 0x10, 1, 40, 6}, {40, 2, 0x10, 41, 80, 6} };
        sectorsPerTrack = { 40 };
    };

    //virtual uint16_t blocksFree() override;
	virtual uint8_t speedZone( uint8_t track) override { return 0; };

protected:

private:
    friend class D81File;
};


/********************************************************
 * File implementations
 ********************************************************/

class D81File: public D64File {
public:
    D81File(std::string path, bool is_dir = true) : D64File(path, is_dir) {};

    MIStream* createIStream(std::shared_ptr<MIStream> containerIstream) override;
};



/********************************************************
 * FS
 ********************************************************/

class D81FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D81File(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".d81", fileName);
    }

    D81FileSystem(): MFileSystem("d81") {};
};


#endif /* MEDIA_CBM_D81 */
