// .D82 - The D82 disk image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC363
// https://ist.uwaterloo.ca/~schepers/formats/D80-D82.TXT
//


#ifndef MEDIA_CBM_D82
#define MEDIA_CBM_D82

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D82IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D82IStream(std::shared_ptr<MIStream> is) : D64IStream(is) 
    {
        // D82 Offsets
        directory_header_offset = {39, 0, 0x06};
        directory_list_offset = {39, 1, 0x00};
        block_allocation_map = { {38, 0, 0x06, 1, 50, 5}, {38, 3, 0x06, 51, 100, 5}, {38, 3, 0x06, 101, 150, 5}, {38, 3, 0x06, 151, 154, 5} };
        sectorsPerTrack = { 23, 25, 27, 29 };
    };

    //virtual uint16_t blocksFree() override;
	virtual uint8_t speedZone( uint8_t track) override
	{
        if ( track < 78 )
		    return (track < 39) + (track < 53) + (track < 64);
        else
            return (track < 116) + (track < 130) + (track < 141);
	};

protected:

private:
    friend class D82File;
};


/********************************************************
 * File implementations
 ********************************************************/

class D82File: public D64File {
public:
    D82File(std::string path, bool is_dir = true) : D64File(path, is_dir) {};

    MIStream* createIStream(std::shared_ptr<MIStream> containerIstream) override;
};



/********************************************************
 * FS
 ********************************************************/

class D82FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D82File(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".d82", fileName);
    }

    D82FileSystem(): MFileSystem("d82") {};
};


#endif /* MEDIA_CBM_D82 */
