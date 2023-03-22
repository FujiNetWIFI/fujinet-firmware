// .DNP - CMD hard Disk Native Partition
// https://ist.uwaterloo.ca/~schepers/formats/D2M-DNP.TXT
//

#ifndef MEATLOAF_MEDIA_DNP
#define MEATLOAF_MEDIA_DNP

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class DNPIStream : public D64IStream {
    // override everything that requires overriding here

public:
    DNPIStream(std::shared_ptr<MStream> is) : D64IStream(is) 
    {
        // DNP Offsets
        directory_header_offset = {1, 0, 0x04};
        directory_list_offset = {1, 0, 0x20}; // Read this offset to get t/s link to start of directory
        block_allocation_map = { {1, 2, 0x10, 1, 255, 8} };
        sectorsPerTrack = { 255 };
    };

    //virtual uint16_t blocksFree() override;
	virtual uint8_t speedZone( uint8_t track) override { return 0; };

protected:

private:
    friend class DNPFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class DNPFile: public D64File {
public:
    DNPFile(std::string path, bool is_dir = true) : D64File(path, is_dir) {};

    MStream* createIStream(std::shared_ptr<MStream> containerIstream) override;
};



/********************************************************
 * FS
 ********************************************************/

class DNPFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new DNPFile(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".dnp", fileName);
    }

    DNPFileSystem(): MFileSystem("dnp") {};
};


#endif /* MEATLOAF_MEDIA_DNP */
