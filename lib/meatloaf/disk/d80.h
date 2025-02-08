// .D80 - This is a sector-for-sector copy of an 8050 floppy disk
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC360
// https://ist.uwaterloo.ca/~schepers/formats/D80-D82.TXT
//


#ifndef MEATLOAF_MEDIA_D80
#define MEATLOAF_MEDIA_D80

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D80MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D80MStream(std::shared_ptr<MStream> is) : D64MStream(is)
    {
        // D80 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                38,     // track
                0,      // sector
                0x06,   // offset
                1,      // start_track
                50,     // end_track
                5       // byte_count
            },
            {
                38,     // track
                3,      // sector
                0x06,   // offset
                51,     // start_track
                77,     // end_track
                5       // byte_count
            } 
        };

        Partition p = {
            39,    // track
            0,     // sector
            0x06,  // header_offset
            39,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 23, 25, 27, 29 };
    };

    virtual uint8_t speedZone(uint8_t track) override
    {
        return (track < 40) + (track < 54) + (track < 65);
    };

protected:

private:
    friend class D80MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D80MFile: public D64MFile {
public:
    D80MFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) 
    {
        size = 533248; // Default - 77 tracks no errors
    };

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new D80MStream(containerIstream);
    }
};



/********************************************************
 * FS
 ********************************************************/

class D80MFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D80MFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".d80", fileName);
    }

    D80MFileSystem(): MFileSystem("d80") {};
};


#endif /* MEATLOAF_MEDIA_D80 */
