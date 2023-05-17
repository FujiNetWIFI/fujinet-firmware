// .D64 - The D64 disk image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC345
// https://ist.uwaterloo.ca/~schepers/formats/D64.TXT
// https://ist.uwaterloo.ca/~schepers/formats/GEOS.TXT
// https://www.lemon64.com/forum/viewtopic.php?t=70024&start=0 (File formats = Why is D64 not called D40/D41)
//  - disucssion of disk id in sector missing from d64 file format is interesting
// https://www.c64-wiki.com/wiki/Disk_Image
// http://unusedino.de/ec64/technical3.html
//

#ifndef MEATLOAF_MEDIA_D64
#define MEATLOAF_MEDIA_D64

#include "meat_io.h"

#include <map>
#include <bitset>

#include "string_utils.h"
#include "cbm_media.h"


/********************************************************
 * Streams
 ********************************************************/

class D64IStream : public CBMImageStream {

protected:

    struct BlockAllocationMap {
        uint8_t track;
        uint8_t sector;
        uint8_t offset;
        uint8_t start_track;
        uint8_t end_track;
        uint8_t byte_count;
    };

    struct Partition {
        uint8_t header_track;
        uint8_t header_sector;
        uint8_t header_offset;
        uint8_t directory_track;
        uint8_t directory_sector;
        uint8_t directory_offset;
        std::vector<BlockAllocationMap> block_allocation_map;
    };

    struct Header {
        char disk_name[16];
        char unused[2];
        char id_dos[5];
    };

    struct Entry {
        uint8_t next_track;
        uint8_t next_sector;
        uint8_t file_type;
        uint8_t start_track;
        uint8_t start_sector;
        char filename[16];
        uint8_t rel_start_track;   // Or GOES info block start track
        uint8_t rel_start_sector;  // Or GEOS info block start sector
        uint8_t rel_record_length; // Or GEOS file structure (Sequential / VLIR file)
        uint8_t geos_type;         // $00 - Non-GEOS (normal C64 file)
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        uint8_t minute;
        uint16_t blocks;
    };

    std::vector<Partition> partitions;
    std::vector<uint8_t> sectorsPerTrack = { 17, 18, 19, 21 };

public:
    D64IStream(std::shared_ptr<MStream> is) : CBMImageStream(is) 
    {
        // D64 Partition Info
        std::vector<BlockAllocationMap> b = { 
            {
                18,     // track
                0,      // sector
                0x04,   // offset
                1,      // start_track
                35,     // end_track
                4       // byte_count
            } 
        };

        Partition p = {
            18,    // track
            0,     // sector
            0x90,  // header_offset
            18,    // directory_track
            1,     // directory_sector
            0x00,  // directory_offset
            b      // block_allocation_map
        };
        partitions.clear();
        partitions.push_back(p);
        sectorsPerTrack = { 17, 18, 19, 21 };
        block_size = 256;


        uint32_t size = containerStream->size();
        switch (size + media_header_size) 
        {
            case 174848: // 35 tracks no errors
                break;

            case 175531: // 35 w/ errors
                error_info = true;
                break;

            case 196608: // 40 tracks no errors
                partitions[partition].block_allocation_map[0].end_track = 40;
                break;

            case 197376: // 40 w/ errors
                partitions[partition].block_allocation_map[0].end_track = 40;
                error_info = true;
                break;

            case 205312: // 42 tracks no errors
                partitions[partition].block_allocation_map[0].end_track = 42;
                break;

            case 206114: // 42 w/ errors
                partitions[partition].block_allocation_map[0].end_track = 42;
                error_info = true;
                break;
        }

        // Get DOS Version

        // Extend BAM Info for DOLPHIN, SPEED, and ProLogic DOS
        // The location of the extra BAM information in sector 18/0, for 40 track images, 
        // will be different depending on what standard the disks have been formatted with. 
        // SPEED DOS stores them from $C0 to $D3, DOLPHIN DOS stores them from $AC to $BF 
        // and PrologicDOS stored them right after the existing BAM entries from $90-A3. 
        // PrologicDOS also moves the disk label and ID forward from the standard location 
        // of $90 to $A4. 64COPY and Star Commander let you select from several different 
        // types of extended disk formats you want to create/work with. 

        // // DOLPHIN DOS
        // block_allocation_map += [{
        //     "track": 18,
        //     "sector": 0,
        //     "offset": 0xAC,
        //     "start_track": 36,
        //     "end_track": 40,
        //     "byte_count": 4
        // }];

        // // SPEED DOS
        // block_allocation_map += [{
        //     "track": 18,
        //     "sector": 0,
        //     "offset": 0xC0,
        //     "start_track": 36,
        //     "end_track": 40,
        //     "byte_count": 4
        // }];

        // // PrologicDOS
        // block_allocation_map += [{
        //     "track": 18,
        //     "sector": 0,
        //     "offset": 0xC0,
        //     "start_track": 36,
        //     "end_track": 40,
        //     "byte_count": 4
        // }];

        //getBAMMessage();

    };


    virtual uint16_t blocksFree();

	virtual uint8_t speedZone( uint8_t track)
	{
		return (track < 18) + (track < 25) + (track < 31);
	};

    //bool seekSector( uint16_t index ) override;
    bool seekSector( uint8_t track, uint8_t sector, size_t offset = 0 );
    bool seekSector( std::vector<uint8_t> trackSectorOffset );

    void seekHeader() override {
        seekSector( 
            partitions[partition].header_track, 
            partitions[partition].header_sector, 
            partitions[partition].header_offset 
        );
        containerStream->read((uint8_t*)&header, sizeof(header));
    }

    bool seekNextImageEntry() override {
        return seekEntry( entry_index + 1 );
    }



    virtual bool seekPath(std::string path) override;
    size_t readFile(uint8_t* buf, size_t size) override;

    Header header;      // Directory header data
    Entry entry;        // Directory entry data

    uint8_t dos_version = 0x41;
    bool error_info = false;
    std::string bam_message = "";

    uint8_t partition = 0;
    uint8_t track = 0;
    uint8_t sector = 0;
    uint16_t offset = 0;
    uint64_t blocks_free = 0;

    uint8_t next_track = 0;
    uint8_t next_sector = 0;
    uint8_t sector_offset = 0;

private:
    void sendListing();

    bool seekEntry( std::string filename );
    bool seekEntry( uint32_t index = 0 );


    std::string readBlock( uint8_t track, uint8_t sector );
    bool writeBlock( uint8_t track, uint8_t sector, std::string data );    
    bool allocateBlock( uint8_t track, uint8_t sector );
    bool deallocateBlock( uint8_t track, uint8_t sector );


    // uint8_t d64_get_type(uint16_t imgsize)
    // {
    //     switch (imgsize)
    //     {
    //         // D64
    //         case 174848:  // 35 tracks no errors
    //         case 175531:  // 35 w/ errors
    //         case 196608:  // 40 tracks no errors
    //         case 197376:  // 40 w/ errors
    //         case 205312:  // 42 tracks no errors
    //         case 206114:  // 42 w/ errors
    //             return D64_TYPE_D64;

    //         // D71
    //         case 349696:  // 70 tracks no errors
    //         case 351062:  // 70 w/ errors
    //             return D64_TYPE_D71;

    //         // D81
    //         case 819200:  // 80 tracks no errors
    //         case 822400:  // 80 w/ errors
    //             return D64_TYPE_D81;
    //     }

    //     return D64_TYPE_UNKNOWN;
    // }

    // Disk
    friend class D64File;
    friend class D71File;
    friend class D80File;
    friend class D81File;
    friend class D82File;
    friend class D8BFile;
    friend class DNPFile;    
};


/********************************************************
 * File implementations
 ********************************************************/

class D64File: public MFile {
public:

    D64File(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        mstr::toASCII(media_image);
    };
    
    ~D64File() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* createIStream(std::shared_ptr<MStream> containerIstream) override;

    std::string petsciiName() override {
        // It's already in PETSCII
        mstr::replaceAll(name, "\\", "/");
        return name;
    }

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override;
    bool remove() override { return false; };
    bool rename(std::string dest) { return false; };
    time_t getLastWrite() override;
    time_t getCreationTime() override;
    uint32_t size() override;     

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class D64FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new D64File(path);
    }

    bool handles(std::string fileName) {
        //Serial.printf("handles w dnp %s %d\n", fileName.rfind(".dnp"), fileName.length()-4);
        return byExtension(".d64", fileName);
    }

    D64FileSystem(): MFileSystem("d64") {};
};


#endif /* MEATLOAF_MEDIA_D64 */
