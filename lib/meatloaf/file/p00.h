// .P00/P** - P00/S00/U00/R00 (Container files for the PC64 emulator)
// https://ist.uwaterloo.ca/~schepers/formats/PC64.TXT
//

#ifndef MEATLOAF_MEDIA_P00
#define MEATLOAF_MEDIA_P00

#include "../meatloaf.h"
#include "../meat_media.h"

/********************************************************
 * Streams
 ********************************************************/

class P00MStream : public MMediaStream {
    // override everything that requires overriding here

public:
    P00MStream(std::shared_ptr<MStream> is) : MMediaStream(is) {
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

    bool readHeader() override {
        containerStream->seek(0x00);
        if (containerStream->read((uint8_t*)&header, sizeof(header)))
            return true;

        return false;
    }
    bool getNextImageEntry() override {
        if ( entry_index == 0 ) {
            entry_index = 1;
            readHeader();

            _size = ( containerStream->size() - sizeof(header) );

            return true;
        }
        return false;
    }

    // For files with no directory structure
    // tap, crt, tar
    std::string seekNextEntry() override {
        seekCalled = true;
        if ( getNextImageEntry() ) {
            return header.filename;
        }
        return "";
    };

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

    Header header;

private:
    friend class P00MFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class P00MFile: public MFile {
public:

    P00MFile(std::string path, bool is_dir = false): MFile(path) {};
    
    ~P00MFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new P00MStream(containerIstream);
    }

    bool isDirectory() override { return false; };;
    bool rewindDirectory() override { return false; };;
    MFile* getNextFileInDir() override { return nullptr; };;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };

    bool isDir = false;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class P00MFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new P00MFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".p00", fileName);
    }

    P00MFileSystem(): MFileSystem("p00") {};
};

#endif // MEATLOAF_MEDIA_P00