#ifndef MEATLOAF_DEVICE_TNFS
#define MEATLOAF_DEVICE_TNFS

#include "meat_io.h"

#include "fnFS.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>


/********************************************************
 * MFile
 ********************************************************/

class TNFSFile: public MFile
{
friend class TNFSIStream;

public:
    std::string basepath = "";
    
    TNFSFile(std::string path) {

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            seekEntry( name );

        if (!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        //Debug_printv("basepath[%s] path[%s] valid[%d]", basepath.c_str(), this->path.c_str(), m_isNull);
    };
    ~TNFSFile() {
        //Serial.printf("*** Destroying flashfile %s\r\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);
    bool isDirectory() override;
    MStream* meatStream() override ; // has to return OPENED stream
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;
    uint32_t size() override ;
    bool remove() override ;
    bool rename(std::string dest);
    MStream* createIStream(std::shared_ptr<MStream> src);

    bool seekEntry( std::string filename );

protected:
    DIR* dir;
    bool dirOpened = false;

private:
    FileSystem *_fs = nullptr;

    virtual void openDir(std::string path);
    virtual void closeDir();

    bool _valid;
    std::string _pattern;

    bool pathValid(std::string path);

};


/********************************************************
 * TNFSHandle
 ********************************************************/

class TNFSHandle {
public:
    //int rc;
    FILE* file_h = nullptr;

    TNFSHandle() 
    {
        //Debug_printv("*** Creating flash handle");
        memset(&file_h, 0, sizeof(file_h));
    };
    ~TNFSHandle();
    void obtain(std::string localPath, std::string mode);
    void dispose();

private:
    int flags = 0;
};


/********************************************************
 * MStream I
 ********************************************************/

class TNFSIStream: public MStream {
public:
    TNFSIStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<TNFSHandle>();
        url = path;
    }
    ~TNFSIStream() override {
        close();
    }

    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

    virtual bool seek(uint32_t pos) override;
    virtual bool seek(uint32_t pos, int mode) override;    

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

    bool isOpen();

protected:
    std::string localPath;

    std::unique_ptr<TNFSHandle> handle;
};


/********************************************************
 * MFileSystem
 ********************************************************/

class TNFSFileSystem: public MFileSystem 
{
    MFile* getFile(std::string path) override {
        return new TNFSFile(path);
    }

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"tnfs:", false) )
            return true;

        return false;
    }
public:
    TNFSFileSystem(): MFileSystem("tnfs") {};

};


#endif // MEATLOAF_DEVICE_TNFS
