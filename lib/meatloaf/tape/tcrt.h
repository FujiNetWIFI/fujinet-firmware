// .TCRT - Tapecart File System
// https://github.com/ikorb/tapecart
// https://github.com/ikorb/tapecart/blob/master/doc/TCRT%20Format.md
// https://github.com/alexkazik/tapecart-browser/blob/master/doc/Tapecart-FileSystem.md


#ifndef MEATLOAF_MEDIA_TCRT
#define MEATLOAF_MEDIA_TCRT

#include "meat_io.h"
#include "cbm_media.h"


/********************************************************
 * Streams
 ********************************************************/

class TCRTIStream : public CBMImageStream {
    // override everything that requires overriding here

public:
    TCRTIStream(std::shared_ptr<MStream> is) : CBMImageStream(is) {};

protected:
    struct Header {
        char disk_name[16];
    };

    struct Entry {
        char filename[16];
        uint8_t file_type;
        uint8_t file_start_address[2]; // from tcrt file system at 0xD8
        uint8_t file_size[3];
        uint8_t file_load_address[2];
        uint16_t bundle_compatibility;
        uint16_t bundle_main_start;
        uint16_t bundle_main_length;
        uint16_t bundle_main_call_address;
    };

    void seekHeader() override {
        containerStream->seek(0x18);
        containerStream->read((uint8_t*)&header, sizeof(header));
    }

    bool seekNextImageEntry() override {
        return seekEntry(entry_index + 1);
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;
    bool seekPath(std::string path) override;

    uint16_t readFile(uint8_t* buf, uint16_t size) override;

    Header header;
    Entry entry;

    // Translate TCRT file type to standard CBM file type
    uint8_t mapType(uint8_t file_type)
    {
        // Type
        // 0x00 - 0x3f: An program which can be loaded into the C64 and executed.
        // The load address from the FS is used (not stored in the file!). The load address + size must fit into the C64.
        // 0x00: general program
        // 0x01: game
        // 0x02: utility
        // 0x03: multimedia
        // 0x04: demo
        // 0x05: image
        // 0x06: tune
        // 0x38-0x3f: private use area
        if ( file_type <= 0x3F )
            return 0x82; // PRG

        // 0x40 - 0x7f: A bundled file (see below) Same types as 0x00-0x3f (just for a bundle and not a prg)
        // This mode is designed for games that need to load further files or store data like high scores or save games.
        // It also cen be used for subdirectories.
        if ( file_type >= 0x40 && file_type <= 0x7F )
            return 0x86; // DIR

        // 0x80 - 0xef: Data files may not displayed by a browser.
        // 0x80: general data
        // 0x81: text file (lower PETSCII)
        // 0x82: koala image
        // 0x83: hires image
        // 0x84: fli image (multicolor)
        if ( file_type == 0x81 )
            return 0x81; // SEQ
        else if ( file_type >= 0x80 && file_type <= 0xEF )
            return 0x82; // PRG

        // 0xf0: separator - this name should be displayed just as a separator. size must be 0, all other info data (start, load address) is undefined
        if ( file_type == 0xF0 )
            return 0x82; // PRG

        // 0xfe: System file! This file is (part of) the program that launches at power up.
        // No other program should ever rename/delete this file or relocate the blocks. 
        // (The entry within the FS however can be moved around. This may cover the FS itself, but it's not required.)
        //if ( file_type == 0xFE )
        //    return 0x00; // DEL

        // 0xff: Marker for the first free entry.

        // All types not mentioned above are reserved.
        return 0x00; // DEL
    }

private:
    friend class TCRTFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class TCRTFile: public MFile {
public:

    TCRTFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~TCRTFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override;

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
    uint32_t size() override;

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class TCRTFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new TCRTFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".tcrt", fileName);
    }

    TCRTFileSystem(): MFileSystem("tcrt") {};
};


#endif /* MEATLOAF_MEDIA_TCRT */
