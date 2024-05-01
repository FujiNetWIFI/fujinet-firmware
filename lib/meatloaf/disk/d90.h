// .D90 - The D90 image is bit-for-bit copy of the hard drives in the D9090 and D9060
//
// https://vice-emu.sourceforge.io/vice_16.html#SEC429
// http://www.baltissen.org/newhtm/diskimag.htm
//


#ifndef MEATLOAF_MEDIA_D90
#define MEATLOAF_MEDIA_D90

#include "../meatloaf.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class D90MStream : public D64MStream {
    // override everything that requires overriding here

public:
    D90MStream(std::shared_ptr<MStream> is) : D64MStream(is)
    {
        // D90 Partition Info
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
        sectorsPerTrack = { 17, 18, 19, 21 };

        // this.size = data.media_data.length;
        // switch (this.size + this.media_header_size) {
        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
             case 5013504:  // D9060
                 sectorsPerTrack = { (4 * 32) }; // Heads * Sectors
                 break;

             case 7520256:  // D9090
                sectorsPerTrack = { (6 * 32) }; // Heads * Sectors
                 break;
        }

        // this.seek(0x04);
        seek( 0x04 );
        // this.partitions[0].directory_track = this.read();
        partitions[0].directory_track = read();
        // this.partitions[0].directory_sector = this.read();
        partitions[0].directory_sector = read();
        // this.partitions[0].track = this.read();
        partitions[0].header_track = read();
        // this.partitions[0].sector = this.read();
        partitions[0].header_sector = read();
        // this.partitions[0].block_allocation_map[0].track = this.read();
        partitions[0].block_allocation_map[0].track = read();
        // this.partitions[0].block_allocation_map[0].sector = this.read();
        partitions[0].block_allocation_map[0].sector = read();
    };

	virtual uint8_t speedZone(uint8_t track) override
	{
        if ( track < 78 )
		    return (track < 39) + (track < 53) + (track < 64);
        else
            return (track < 116) + (track < 130) + (track < 141);
	};

protected:

private:
    friend class D90MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class D90MFile: public D64MFile {
public:
    D90MFile(std::string path, bool is_dir = true) : D64MFile(path, is_dir) {};

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new D90MStream(containerIstream);
    }
};



/********************************************************
 * FS
 ********************************************************/

class D90MFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D90MFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(
            {
                ".d90",
                ".d60"
            }, 
            fileName
        );
    }

    D90MFileSystem(): MFileSystem("d90") {};
};


#endif /* MEATLOAF_MEDIA_D90 */
