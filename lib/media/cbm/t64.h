// .T64 - The T64 tape image format
// https://vice-emu.sourceforge.io/vice_17.html#SEC331
// https://ist.uwaterloo.ca/~schepers/formats/T64.TXT
//


#ifndef MEDIA_CBM_T64
#define MEDIA_CBM_T64

//#include "meat_io.h"
#include "cbm_image.h"


/********************************************************
 * Streams
 ********************************************************/

class T64IStream : public CBMImageStream {
    // override everything that requires overriding here

public:
    T64IStream(std::shared_ptr<MIStream> is) : CBMImageStream(is) { };

protected:
    struct Header {
        char disk_name[24];
    };

    struct Entry {
        uint8_t entry_type;
        uint8_t file_type;
        uint8_t start_address[2];
        uint8_t end_address[2];
        uint16_t free_1;
        uint32_t data_offset;
        uint32_t free_2;
        char filename[16];
    };

    void seekHeader() override {
        containerStream->seek(0x28);
        containerStream->read((uint8_t*)&header, 24);
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
    friend class T64File;
};


/********************************************************
 * File implementations
 ********************************************************/

class T64File: public MFile {
public:

    T64File(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;
    };
    
    ~T64File() {
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

class T64FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new T64File(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".t64", fileName);
    }

    T64FileSystem(): MFileSystem("t64") {};
};


#endif /* MEDIA_CBM_T64 */
