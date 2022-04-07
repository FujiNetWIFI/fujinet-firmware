// .D8B - Backbit D8B disk format
// https://www.backbit.io/downloads/Docs/BackBit%20Cartridge%20Documentation.pdf#page=9
// https://github.com/evietron/BackBit-Tool
//

#ifndef MEDIA_CBM_D8B
#define MEDIA_CBM_D8B

//#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D8BIStream : public D64IStream {
    // override everything that requires overriding here

public:
    D8BIStream(std::shared_ptr<MIStream> is) : D64IStream(is) 
    {
        // D8B Offsets
        directory_header_offset = {1, 0, 0x04};
        directory_list_offset = {1, 4, 0x00};
        block_allocation_map = { {1, 1, 0x00, 1, 40, 18} };
        sectorsPerTrack = { 136 };
    };

    //virtual uint16_t blocksFree() override;
	virtual uint8_t speedZone( uint8_t track) override { return 0; };

protected:

private:
    friend class D8BFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D8BFile: public D64File {
public:
    D8BFile(std::string path, bool is_dir = true) : D64File(path, is_dir) {};

    MIStream* createIStream(std::shared_ptr<MIStream> containerIstream) override;
};



/********************************************************
 * FS
 ********************************************************/

class D8BFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D8BFile(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".d8b", fileName);
    }

    D8BFileSystem(): MFileSystem("d8b") {};
};


#endif /* MEDIA_CBM_D8B */
