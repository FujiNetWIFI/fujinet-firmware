// .TCRT - Tapecart File System
// https://github.com/ikorb/tapecart
// https://github.com/ikorb/tapecart/blob/master/doc/TCRT%20Format.md
// https://github.com/alexkazik/tapecart-browser/blob/master/doc/Tapecart-FileSystem.md


#ifndef MEATLOAF_MEDIA_TCRT
#define MEATLOAF_MEDIA_TCRT

#include "meat_io.h"
#include "meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class TCRTIStream : public MImageStream {
    // override everything that requires overriding here

public:
    TCRTIStream(std::shared_ptr<MStream> is) : MImageStream(is) {};

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

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

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
