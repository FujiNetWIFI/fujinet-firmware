// .TCRT - Tapecart File System
// https://github.com/ikorb/tapecart
// https://github.com/ikorb/tapecart/blob/master/doc/TCRT%20Format.md
// https://github.com/alexkazik/tapecart-browser/blob/master/doc/Tapecart-FileSystem.md


#ifndef MEDIA_CBM_TCRT
#define MEDIA_CBM_TCRT

#include "meat_io.h"
#include "d64.h"


/********************************************************
 * Streams
 ********************************************************/

class TCRTIStream : public CBMImageStream {
    // override everything that requires overriding here

public:
    TCRTIStream(std::shared_ptr<MIStream> is) : CBMImageStream(is) {};

protected:
    struct Header {
        char disk_name[16];
    };

    struct Entry {
        char filename[16];
        uint8_t file_type;
        uint16_t data_offset;
        uint8_t file_size[3];
        uint16_t load_address;
        uint16_t bundle_compatibility;
        uint16_t bundle_main_start;
        uint16_t bundle_main_length;
        uint16_t bundle_main_call_address;
    };

    void seekHeader() override {
        Debug_printv("here");
        containerStream->seek(0x18);
        containerStream->read((uint8_t*)&header, sizeof(header));
    }

    bool seekNextImageEntry() override {
        return seekEntry(entry_index + 1);
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( size_t index ) override;

    size_t readFile(uint8_t* buf, size_t size) override;
    bool seekPath(std::string path) override;

    Header header;
    Entry entry;

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
    };
    
    ~TCRTFile() {
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

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
    size_t size() override;

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

    bool handles(std::string fileName) {
        return byExtension(".tcrt", fileName);
    }

    TCRTFileSystem(): MFileSystem("tcrt") {};
};


#endif /* MEDIA_CBM_TCRT */
