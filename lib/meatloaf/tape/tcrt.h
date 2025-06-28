// .TCRT - Tapecart File System
// https://github.com/ikorb/tapecart
// https://github.com/ikorb/tapecart/blob/master/doc/TCRT%20Format.md
// https://github.com/alexkazik/tapecart-browser/blob/master/doc/Tapecart-FileSystem.md


#ifndef MEATLOAF_MEDIA_TCRT
#define MEATLOAF_MEDIA_TCRT

#include "../meatloaf.h"
#include "../meat_media.h"


/********************************************************
 * Streams
 ********************************************************/

class TCRTMStream : public MMediaStream {
    // override everything that requires overriding here

public:
    TCRTMStream(std::shared_ptr<MStream> is) : MMediaStream(is) {};

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

    bool readHeader() override {
        containerStream->seek(0x18);
        if (containerStream->read((uint8_t*)&header, sizeof(header)))
            return true;

        return false;
    }

    bool seekEntry( std::string filename ) override;
    bool seekEntry( uint16_t index ) override;
    bool seekPath(std::string path) override;

    uint32_t readFile(uint8_t* buf, uint32_t size) override;
    uint32_t writeFile(uint8_t* buf, uint32_t size) override { return 0; };

    Header header;
    Entry entry;

    std::string decodeType(uint8_t file_type, bool show_hidden = false) override;

private:
    friend class TCRTMFile;
};


/********************************************************
 * File implementations
 ********************************************************/

class TCRTMFile: public MFile {
public:

    TCRTMFile(std::string path, bool is_dir = true): MFile(path) {
        isDir = is_dir;

        media_image = name;
        isPETSCII = true;
    };
    
    ~TCRTMFile() {
        // don't close the stream here! It will be used by shared ptr D64Util to keep reading image params
    }

    MStream* getDecodedStream(std::shared_ptr<MStream> containerIstream) override
    {
        Debug_printv("[%s]", url.c_str());

        return new TCRTMStream(containerIstream);
    }

    bool isDirectory() override;
    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override { return false; };

    bool exists() override { return true; };
    bool remove() override { return false; };
    bool rename(std::string dest) override { return false; };
    time_t getLastWrite() override { return 0; };
    time_t getCreationTime() override { return 0; };

    bool isDir = true;
    bool dirIsOpen = false;
};



/********************************************************
 * FS
 ********************************************************/

class TCRTMFileSystem: public MFileSystem
{
public:
    MFile* getFile(std::string path) override {
        return new TCRTMFile(path);
    }

    bool handles(std::string fileName) override {
        return byExtension(".tcrt", fileName);
    }

    TCRTMFileSystem(): MFileSystem("tcrt") {};
};


#endif /* MEATLOAF_MEDIA_TCRT */
