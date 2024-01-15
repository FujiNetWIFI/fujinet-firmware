// .D81 - This is a byte for byte copy of a physical 1581 disk
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC354
// https://ist.uwaterloo.ca/~schepers/formats/D81.TXT
//


#ifndef MEATLOAF_MEDIA_D81
#define MEATLOAF_MEDIA_D81

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D81IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D81IStream(std::shared_ptr<MStream> is) : D64IStream(is) 
    {
        // D81 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                40,     // track
                1,      // sector
                0x10,   // offset
                1,      // start_track
                40,     // end_track
                6       // byte_count
            },
            {
                40,     // track
                0,      // sector
                0x10,   // offset
                41,     // start_track
                80,     // end_track
                6       // byte_count
            } 
        };

        Partition p = {
            40,    // track
            0,     // sector
            0x04,  // header_offset
            40,    // directory_track
            3,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 40 };
        dos_rom = "dos1581";
        has_subdirs = true;

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 819200:  // 80 tracks no errors
                break;

            case 822400:  // 80 w/ errors
                error_info = true;
                break;
        }
    };

	virtual uint8_t speedZone(uint8_t track) override { return 0; };

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

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new D81IStream(containerIstream);
    }
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

    bool handles(std::string fileName) override {
        return byExtension(".d81", fileName);
    }

    D81FileSystem(): MFileSystem("d81") {};
};


#endif /* MEATLOAF_MEDIA_D81 */
