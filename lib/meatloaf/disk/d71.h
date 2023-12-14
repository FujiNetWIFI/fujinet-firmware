// .D71 - 1571 disk image format
//
// https://vice-emu.sourceforge.io/vice_17.html#SEC373
// https://ist.uwaterloo.ca/~schepers/formats/D71.TXT
//


#ifndef MEATLOAF_MEDIA_D71
#define MEATLOAF_MEDIA_D71

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D71IStream : public D64IStream {
    // override everything that requires overriding here

public:
    D71IStream(std::shared_ptr<MStream> is) : D64IStream(is) 
    {
        // D71 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                18,     // track
                0,      // sector
                0x04,   // offset
                1,      // start_track
                35,     // end_track
                4       // byte_count
            },
            {
                53,     // track
                0,      // sector
                0x00,   // offset
                36,     // start_track
                70,     // end_track
                3       // byte_count
            } 
        };

        Partition p = {
            1,     // track
            0,     // sector
            0x04,  // header_offset
            1,     // directory_track
            4,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };
        dos_rom = "dos1571";

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 349696: // 70 tracks no errors
                break;

            case 351062: // 70 w/ errors
                error_info = true;
                break;
        }
    };

	virtual uint8_t speedZone( uint8_t track) override
	{
        if ( track < 35 )
		    return (track < 18) + (track < 25) + (track < 31);
        else
            return (track < 53) + (track < 60) + (track < 66);
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

    MStream* createIStream(std::shared_ptr<MStream> containerIstream) override;
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


#endif /* MEATLOAF_MEDIA_D71 */
