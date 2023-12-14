// .D82 - This is a sector-for-sector copy of an 8250 floppy disk
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC363
// https://ist.uwaterloo.ca/~schepers/formats/D80-D82.TXT
//


#ifndef MEATLOAF_MEDIA_D82
#define MEATLOAF_MEDIA_D82

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D82IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D82IStream(std::shared_ptr<MStream> is) : D64IStream(is) 
    {
        // D82 Partition Info
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
                100,    // end_track
                5       // byte_count
            },
            {
                38,     // track
                6,      // sector
                0x06,   // offset
                101,    // start_track
                150,    // end_track
                5       // byte_count
            },
            {
                38,     // track
                9,      // sector
                0x06,   // offset
                151,    // start_track
                154,    // end_track
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

	virtual uint8_t speedZone( uint8_t track) override
	{
        if (track < 78)
            return (track < 40) + (track < 54) + (track < 65);
        else
            return (track < 117) + (track < 131) + (track < 142);
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

    MStream* createIStream(std::shared_ptr<MStream> containerIstream) override;
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


#endif /* MEATLOAF_MEDIA_D82 */
