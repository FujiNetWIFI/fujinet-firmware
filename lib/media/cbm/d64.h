// .D64 - The D64 disk image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC345
// https://ist.uwaterloo.ca/~schepers/formats/D64.TXT
// https://ist.uwaterloo.ca/~schepers/formats/GEOS.TXT
// https://www.lemon64.com/forum/viewtopic.php?t=70024&start=0 (File formats = Why is D64 not called D40/D41)
//  - disucssion of disk id in sector missing from d64 file format is interesting
// https://www.c64-wiki.com/wiki/Disk_Image
// http://unusedino.de/ec64/technical3.html
//

#ifndef MEDIA_CBM_D64
#define MEDIA_CBM_D64

//#include "meat_io.h"

#include <map>
#include <bitset>

//#include "string_utils.h"
#include "cbm_image.h"


/********************************************************
 * Streams
 ********************************************************/

class D64IStream : public CBMImageStream {

public:
    D64IStream(std::shared_ptr<MIStream> is) : CBMImageStream(is) {};

protected:

    struct Header {
        char disk_name[16];
        char unused[2];
        char id_dos[5];
    };

    struct BAMInfo {
        uint8_t track;
        uint8_t sector;
        uint8_t offset;
        uint8_t start_track;
        uint8_t end_track;
        uint8_t byte_count;
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


    // D64 Offsets
    std::vector<uint8_t> directory_header_offset = {18, 0, 0x90};
    std::vector<uint8_t> directory_list_offset = {18, 1, 0x00};
    std::vector<BAMInfo> block_allocation_map = { {18, 0, 0x04, 1, 35, 4} };
    std::vector<uint8_t> sectorsPerTrack = { 17, 18, 19, 21 };
    //uint8_t sector_buffer[256] = { 0 };

    bool seekSector( uint8_t track, uint8_t sector, size_t offset = 0 );
    bool seekSector( std::vector<uint8_t> trackSectorOffset = { 0 } );

    void seekHeader() override {
        seekSector(directory_header_offset);
        containerStream->read((uint8_t*)&header, sizeof(header));
    }

    bool seekNextImageEntry() override {
        return seekEntry(entry_index + 1);
    }

    virtual uint16_t blocksFree();

	virtual uint8_t speedZone( uint8_t track)
	{
		return (track < 17) + (track < 24) + (track < 30);
	};

    virtual bool seekPath(std::string path) override;
    size_t readFile(uint8_t* buf, size_t size) override;

    Header header;      // Directory header data
    Entry entry;        // Directory entry data

    uint8_t dos_version;

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
    bool seekEntry( size_t index = 0 );


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
    };
    
    ~D64File() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MIStream* createIStream(std::shared_ptr<MIStream> containerIstream) override;

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
    size_t size() override;     

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


#endif /* MEDIA_CBM_D64 */
