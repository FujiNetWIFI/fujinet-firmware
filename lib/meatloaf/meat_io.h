#ifndef MEATLOAF_FILE
#define MEATLOAF_FILE

#include <memory>
#include <string>
#include <vector>
#include <fstream>

//#include "../../include/debug.h"

//#include "wrappers/iec_buffer.h"

#include "meat_stream.h"
#include "peoples_url_parser.h"
#include "string_utils.h"
#include "U8Char.h"

#define _MEAT_NO_DATA_AVAIL (std::ios_base::eofbit)

static const std::ios_base::iostate ndabit = _MEAT_NO_DATA_AVAIL;


/********************************************************
 * Universal file
 ********************************************************/

class MFile : public PeoplesUrlParser {
public:
    MFile() {}; // only for local FS!!!
    MFile(nullptr_t null) : m_isNull(true) {};
    MFile(std::string path);
    MFile(std::string path, std::string name);
    MFile(MFile* path, std::string name);

    virtual ~MFile() {
        if(streamFile != nullptr) {
        //     Debug_printv("WARNING: streamFile null in '%s' destructor. This MFile was obviously not initialized properly!", url.c_str());
        // }
        // else {
            //Debug_printv("Deleting: [%s]", this->url.c_str());
            delete streamFile;
        }
    };

    bool isPETSCII = false;
    std::string media_header;
    std::string media_id;
    std::string media_archive;
    std::string media_image;
    uint16_t media_blocks_free = 0;
    uint16_t media_block_size = 256;

    bool operator!=(nullptr_t ptr);

    // bool copyTo(MFile* dst);

    // has to return OPENED stream
    virtual MStream* getSourceStream(std::ios_base::openmode mode=std::ios_base::in);
    virtual MStream* getDecodedStream(std::shared_ptr<MStream> src) = 0;

    MFile* cd(std::string newDir);
    MFile* cdParent(std::string = "");
    MFile* cdLocalParent(std::string);
    MFile* cdRoot(std::string);
    MFile* cdLocalRoot(std::string);
    virtual bool isDirectory() = 0;
    virtual bool rewindDirectory() = 0 ;
    virtual MFile* getNextFileInDir() = 0 ;
    virtual bool mkDir() = 0 ;    

    virtual bool exists() { return _exists; };
    virtual bool remove() = 0;
    virtual bool rename(std::string dest) = 0;    
    virtual time_t getLastWrite() = 0 ;
    virtual time_t getCreationTime() = 0 ;
    virtual uint64_t getAvailableSpace();

    virtual uint32_t size() {
        return _size;
    };
    virtual uint32_t blocks() {
        auto s = size();
        if ( s > 0 && s < media_block_size )
            return 1;
        else
            return ( s / media_block_size );
    }

    virtual bool isText() {
        return mstr::isText(extension);
    }

    MFile* streamFile = nullptr;
    std::string pathInStream;

    uint32_t _size = 0;
    uint32_t _exists = true;

protected:
    bool m_isNull;

friend class MFSOwner;
};

/********************************************************
 * Filesystem instance
 * it knows how to create a MFile instance!
 ********************************************************/

class MFileSystem {
public:
    MFileSystem(const char* symbol);
    virtual ~MFileSystem() = 0;
    virtual bool mount() { return true; };
    virtual bool umount() { return true; };
    virtual bool handles(std::string path) = 0;
    virtual MFile* getFile(std::string path) = 0;
    bool isMounted() {
        return _is_mounted;
    }

    static bool byExtension(const char* ext, std::string fileName) {
        return mstr::endsWith(fileName, ext, false);
    }

    static bool byExtension(const std::vector<std::string> &ext, std::string fileName) {
        for ( const auto &e : ext )
        {
            if ( mstr::endsWith(fileName, e.c_str(), false) )
                return true;
        }

        return false;
    }

protected:
    const char* symbol = nullptr;
    bool _is_mounted = false;

    friend class MFSOwner;
};


/********************************************************
 * MFile factory
 ********************************************************/

class MFSOwner {
public:
    static std::vector<MFileSystem*> availableFS;

    static MFile* File(std::string name);
    static MFile* File(std::shared_ptr<MFile> file);
    static MFile* File(MFile* file);

    static MFileSystem* scanPathLeft(std::vector<std::string> paths, std::vector<std::string>::iterator &pathIterator);

    static std::string existsLocal( std::string path );
    static MFileSystem* testScan(std::vector<std::string>::iterator &begin, std::vector<std::string>::iterator &end, std::vector<std::string>::iterator &pathIterator);


    static bool mount(std::string name);
    static bool umount(std::string name);
};

/********************************************************
 * Meat namespace, standard C++ buffers and streams
 ********************************************************/

namespace Meat {
    struct _Unique_mf {
        typedef std::unique_ptr<MFile> _Single_file;
    };

    // Creates a unique_ptr<MFile> for a given url

    /**
    *  @brief  Creates a unique_ptr<MFile> instance froma given url
    *  @param  url  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        New(std::string url) {
            return std::unique_ptr<MFile>(MFSOwner::File(url));
        }

    /**
    *  @brief  Creates a unique_ptr<MFile> instance froma given url
    *  @param  url  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        New(const char* url) {
            return std::unique_ptr<MFile>(MFSOwner::File(std::string(url)));
        }

    /**
    *  @brief  Creates a unique_ptr<MFile> instance froma given MFile
    *  @param  file  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        New(MFile* mFile) {
            return std::unique_ptr<MFile>(MFSOwner::File(mFile->url));
        }

    /**
    *  @brief  Wraps MFile* into unique_ptr<MFile> so it closes itself as required
    *  @param  file  The url to the file.
    *  @return  @c unique_ptr<MFile>
    */
    template<class MFile>
        typename _Unique_mf::_Single_file
        Wrap(MFile* file) {
            return std::unique_ptr<MFile>(file);
        }

}

#endif // MEATLOAF_FILE
