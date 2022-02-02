// .D80 - The D80 disk image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC360
// https://ist.uwaterloo.ca/~schepers/formats/D80-D82.TXT
//


#ifndef MEDIA_CBM_D80
#define MEDIA_CBM_D80

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D80IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D80IStream(std::shared_ptr<MIStream> is) : D64IStream(is) 
    {
        // D80 Offsets
        directory_header_offset = {39, 0, 0x06};
        directory_list_offset = {39, 1, 0x00};
        block_allocation_map = { {38, 0, 0x06, 1, 50, 5}, {38, 3, 0x06, 51, 77, 5} };
        sectorsPerTrack = { 23, 25, 27, 29 };
    };

    //virtual uint16_t blocksFree() override;
	virtual uint8_t speedZone( uint8_t track) override
	{
        return (track < 39) + (track < 53) + (track < 64);
	};

protected:

private:
    friend class D80File;
};


/********************************************************
 * File implementations
 ********************************************************/

class D80File: public D64File {
public:
    D80File(std::string path, bool is_dir = true) : D64File(path, is_dir) {};

    MIStream* createIStream(std::shared_ptr<MIStream> containerIstream) override;
};



/********************************************************
 * FS
 ********************************************************/

class D80FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D80File(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".d80", fileName);
    }

    D80FileSystem(): MFileSystem("d80") {};
};


#endif /* MEDIA_CBM_D80 */
