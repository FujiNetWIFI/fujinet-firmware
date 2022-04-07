// .D71 - The D71 disk image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC351
// https://ist.uwaterloo.ca/~schepers/formats/D71.TXT
//


#ifndef MEDIA_CBM_D71
#define MEDIA_CBM_D71

//#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D71IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D71IStream(std::shared_ptr<MIStream> is) : D64IStream(is) 
    {
        // D71 Offsets
        //directory_header_offset = {18, 0, 0x90};
        //directory_list_offset = {18, 1, 0x00};
        block_allocation_map = { {18, 0, 0x04, 1, 35, 4}, {53, 0, 0x00, 36, 70, 3} };
        //sectorsPerTrack = { 17, 18, 19, 21 };
    };

    //virtual uint16_t blocksFree() override;
	virtual uint8_t speedZone( uint8_t track) override
	{
        if ( track < 35 )
		    return (track < 17) + (track < 24) + (track < 30);
        else
            return (track < 52) + (track < 59) + (track < 65);
	};

protected:

private:
    friend class D71File;
};


/********************************************************
 * File implementations
 ********************************************************/

class D71File: public D64File {
public:
    D71File(std::string path, bool is_dir = true) : D64File(path, is_dir) {};

    MIStream* createIStream(std::shared_ptr<MIStream> containerIstream) override;
};



/********************************************************
 * FS
 ********************************************************/

class D71FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D71File(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".d71", fileName);
    }

    D71FileSystem(): MFileSystem("d71") {};
};


#endif /* MEDIA_CBM_D71 */
