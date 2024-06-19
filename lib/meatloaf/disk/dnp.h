// .DNP - CMD hard Disk Native Partition
//
// https://ist.uwaterloo.ca/~schepers/formats/D2M-DNP.TXT
//

#ifndef MEATLOAF_MEDIA_DNP
#define MEATLOAF_MEDIA_DNP

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class DNPMStream : public D64MStream {
    // override everything that requires overriding here

public:
    DNPMStream(std::shared_ptr<MStream> is) : D64MStream(is) 
    {
        // DNP Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                1,      // track
                2,      // sector
                0x10,   // offset
                1,      // start_track
                255,    // end_track
                8       // byte_count
            } 
        };

        Partition p = {
            1,     // track
            0,     // sector
            0x04,  // header_offset
            1,     // directory_track
            0,     // directory_sector
            0x20,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 256 };
        has_subdirs = true;
    };

    virtual uint8_t speedZone(uint8_t track) override { return 0; };

protected:

private:
    friend class DNPMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class DNPMFile: public D64MFile {
public:
    DNPMFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) {};

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new DNPMStream(containerIstream);
    }
};



/********************************************************
 * FS
 ********************************************************/

class DNPMFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new DNPMFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".dnp", fileName);
    }

    DNPMFileSystem(): MFileSystem("dnp") {};
};


#endif /* MEATLOAF_MEDIA_DNP */
