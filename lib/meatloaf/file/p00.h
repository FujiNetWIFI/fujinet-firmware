// .P00/P** - P00/S00/U00/R00 (Container files for the PC64 emulator)
// https://ist.uwaterloo.ca/~schepers/formats/PC64.TXT
//

#ifndef MEATLOAF_MEDIA_P00
#define MEATLOAF_MEDIA_P00

#include "meat_io.h"
#include "cbm_media.h"

/********************************************************
 * Streams
 ********************************************************/

class P00IStream : public CBMImageStream {
    // override everything that requires overriding here

public:
    P00IStream(std::shared_ptr<MStream> is) : CBMImageStream(is) {
        entry_count = 1;
        seekNextEntry();
    };

protected:
    struct Header {
        char signature[7];
        uint8_t pad1;
        char filename[16];
        uint8_t pad2;
        uint8_t rel_flag;
    };

    void seekHeader() override {
        containerStream->seek(0x00);
        containerStream->read((uint8_t*)&header, sizeof(header));
    }
    bool seekNextImageEntry() override {
        if ( entry_index == 0 ) {
            entry_index = 1;
            seekHeader();

            m_length = ( containerStream->size() - sizeof(header) );
            m_bytesAvailable = m_length;

            return true;
        }
        return false;
    }

    // For files with no directory structure
    // tap, crt, tar
    std::string seekNextEntry() override {
        seekCalled = true;
        if ( seekNextImageEntry() ) {
            return header.filename;
        }
        return "";
    };

    size_t readFile(uint8_t* buf, size_t size) override;

    Header header;

private:
    friend class P00File;
};


/********************************************************
 * File implementations
 ********************************************************/

class P00File: public MFile {
public:

    P00File(std::string path, bool is_dir = false): MFile(path) {
        isDir = is_dir;
    };
    
    ~P00File() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* createIStream(std::shared_ptr<MStream> containerIstream) override;

    std::string petsciiName() override {
        // It's already in PETSCII
        mstr::replaceAll(name, "\\", "/");
        return name;
    }

    bool isDirectory() override { return false; };;
    bool rewindDirectory() override { return false; };;
    MFile* getNextFileInDir() override { return nullptr; };;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };
    uint32_t size() override;

    bool isDir = false;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class P00FileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new P00File(path);
    }

    bool handles(std::string fileName) {
        return byExtension(".p00", fileName);
    }

    P00FileSystem(): MFileSystem("p00") {};
};

#endif // MEATLOAF_MEDIA_P00