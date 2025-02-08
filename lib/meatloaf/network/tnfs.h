#ifndef MEATLOAF_DEVICE_TNFS
#define MEATLOAF_DEVICE_TNFS

#include "meatloaf.h"

#include "fnFS.h"

#include "../../../include/debug.h"

#include "make_unique.h"

#include <dirent.h>
#include <string.h>


/********************************************************
 * MFile
 ********************************************************/

class TNFSMFile: public MFile
{
friend class TNFSMStream;

public:
    std::string basepath = "";
    
    TNFSMFile(std::string path) {

        // Find full filename for wildcard
        if (mstr::contains(name, "?") || mstr::contains(name, "*"))
            readEntry( name );

        if (!pathValid(path.c_str()))
            m_isNull = true;
        else
            m_isNull = false;

        //Debug_printv("basepath[%s] path[%s] valid[%d]", basepath.c_str(), this->path.c_str(), m_isNull);
    };
    ~TNFSMFile() {
        //printf("*** Destroying flashfile %s\r\n", url.c_str());
        closeDir();
    }

    //MFile* cd(std::string newDir);

    MStream* getSourceStream(std::ios_base::openmode mode=std::ios_base::in) override ; // has to return OPENED stream
    MStream* getDecodedStream(std::shared_ptr<MStream> src);
    MStream* createStream(std::ios_base::openmode mode) override;

    bool isDirectory() override;
    time_t getLastWrite() override ;
    time_t getCreationTime() override ;
    bool rewindDirectory() override ;
    MFile* getNextFileInDir() override ;
    bool mkDir() override ;
    bool exists() override ;

    bool remove() override ;
    bool rename(std::string dest);


    bool readEntry( std::string filename );

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

class TNFSMStream: public MStream {
public:
    TNFSMStream(std::string& path) {
        localPath = path;
        handle = std::make_unique<TNFSHandle>();
        url = path;
    }
    ~TNFSMStream() override {
        close();
    }

    // MStream methods
    bool isOpen();
    bool isBrowsable() override { return false; };
    bool isRandomAccess() override { return true; };

    bool open(std::ios_base::openmode mode) override;
    void close() override;

    uint32_t read(uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t *buf, uint32_t size) override;

    virtual bool seek(uint32_t pos) override;
    virtual bool seek(uint32_t pos, int mode) override;    

    virtual bool seekPath(std::string path) override {
        Debug_printv( "path[%s]", path.c_str() );
        return false;
    }


protected:
    std::string localPath;

    std::unique_ptr<TNFSHandle> handle;
};


/********************************************************
 * MFileSystem
 ********************************************************/

class TNFSMFileSystem: public MFileSystem 
{
    MFile* getFile(std::string path) override {
        return new TNFSMFile(path);
    }

    bool handles(std::string name) {
        if ( mstr::equals(name, (char *)"tnfs:", false) )
            return true;

        return false;
    }
public:
    TNFSMFileSystem(): MFileSystem("tnfs") {};

};


#endif // MEATLOAF_DEVICE_TNFS
