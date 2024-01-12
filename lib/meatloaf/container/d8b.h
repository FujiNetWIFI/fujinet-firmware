// .D8B - Backbit D8B disk format
//
// https://www.backbit.io/downloads/Docs/BackBit%20Cartridge%20Documentation.pdf#page=20
// https://github.com/evietron/BackBit-Tool
//

#ifndef MEATLOAF_MEDIA_D8B
#define MEATLOAF_MEDIA_D8B

#include "meat_io.h"
#include "disk/d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D8BIStream : public D64IStream {
    // override everything that requires overriding here

public:
    D8BIStream(std::shared_ptr<MStream> is) : D64IStream(is)
    {
        // D8B Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                1,      // track
                1,      // sector
                0x00,   // offset
                1,      // start_track
                40,     // end_track
                18      // byte_count
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
        sectorsPerTrack = { 136 };

        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 1392640: // 136 sectors per track (deprecated)
                break;

            case 1474560: // 144 sectors per track
                sectorsPerTrack = { 144 };
                break;
        }
    };

	// virtual std::unordered_map<std::string, std::string> info() override { 
    //     return {
    //         {"System", "Commodore"},
    //         {"Format", "D8B"},
    //         {"Media Type", "ARCHIVE"},
    //         {"Tracks", getTrackCount()},
    //         {"Sectors / Blocks", this.getSectorCount()},
    //         {"Sector / Block Size", std::string(block_size)},
    //         {"Format", "Backbit Archive"}
    //     }; 
    // };

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

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override;
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

    bool handles(std::string fileName) override {
        return byExtension(".d8b", fileName);
    }

    D8BFileSystem(): MFileSystem("d8b") {};
};


#endif /* MEATLOAF_MEDIA_D8B */
