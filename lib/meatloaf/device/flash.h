#ifndef TEST_NATIVE
#ifndef MEATLOAF_DEVICE_FLASH
#define MEATLOAF_DEVICE_FLASH

#include "meatloaf.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>

#include "../../include/debug.h"

/********************************************************
 * MFileSystem
 ********************************************************/

class FlashMFileSystem: public MFileSystem 
{
    bool handles(std::string path) override;
    
public:
    FlashMFileSystem() : MFileSystem("FlashFS") {};
    MFile* getFile(std::string path) override;

};



/********************************************************
 * MFile
 ********************************************************/

class FlashMFile: public MFile
{
friend class FlashMStream;

public:
    std::string basepath = "";
    
    FlashMFile(std::string path): MFile(path) {
        // parseUrl( path );

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            seekEntry( name );

        if (!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        //Debug_printv("basepath[%s] path[%s] valid[%d]", basepath.c_str(), this->path.c_str(), m_isNull);
    };
    ~FlashMFile() {
        //Serial.printf("*** Destroying flashfile %s\r\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);
    bool isDirectory() override;
    MStream* getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    MStream* getDecodedStream(std::shared_ptr<MStream> src) override;

    bool rewindDirectory() override;
    MFile* getNextFileInDir() override;
    bool mkDir() override;
    bool exists() override;
    bool remove() override;
    bool rename(std::string dest) override;

    time_t getLastWrite() override;
    time_t getCreationTime() override;
    uint32_t size() override;

    bool seekEntry( std::string filename );

protected:
    DIR* dir;
    bool dirOpened = false;

private:
    virtual void openDir(std::string path);
    virtual void closeDir();

    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);
};


/********************************************************
 * FlashHandle
 ********************************************************/

class FlashHandle {
public:
    //int rc;
    FILE* file_h = nullptr;

    FlashHandle() 
    {
        //Debug_printv("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));
    };
    ~FlashHandle();
    void obtain(std::string localPath, std::string mode);
    void dispose();

private:
    int flags = 0;
};


/********************************************************
 * MStream I
 ********************************************************/

class FlashMStream: public MStream {
public:
    FlashMStream(std::string& path, std::ios_base::openmode m) {
        localPath = path;
        mode = m;
        handle = std::make_unique<FlashHandle>();
        //url = path;
    }
    ~FlashMStream() override {
        close();
    }

    // MStream methods
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

    virtual bool seek(uint32_t pos) override;

    void close() override;
    bool open() override;

    // MStream methods
    //uint8_t read() override;
    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }

    bool isOpen() override;

protected:
    std::string localPath;

    std::unique_ptr<FlashHandle> handle;
};



#endif // MEATLOAF_DEVICE_FLASH
#endif // TEST_NATIVE