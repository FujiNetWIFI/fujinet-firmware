#ifndef MEATLOAF_FILE
#define MEATLOAF_FILE

#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
//#include <unordered_map>

#include "../../include/debug.h"

//#include "wrappers/iec_buffer.h"


#include "peoples_url_parser.h"
#include "string_utils.h"
#include "U8Char.h"

#define _MEAT_NO_DATA_AVAIL (std::ios_base::eofbit)

static const std::ios_base::iostate ndabit = _MEAT_NO_DATA_AVAIL;

/********************************************************
 * Universal stream
 ********************************************************/

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#define SA0 0b00001111
#define SA1 0b00011111
#define SA2 0b00101111
#define SA3 0b00111111
#define SA4 0b01001111
#define SA5 0b01011111
#define SA6 0b01101111
#define SA7 0b01111111
#define SA8 0b10001111
#define SA9 0b10011111
#define SA10 0b10101111
#define SA11 0b10111111
#define SA12 0b11001111
#define SA13 0b11011111
#define SA14 0b11101111
#define SA15 0b11111111
// SA for TCP:
// TCP_NON_BLOCKING = clear bit 4
// TCP_BLOCKING = set bit 4
// TCP_CLENT_SOCKET = clear bit 5
// TCP_SERVER_SOCKET = set bit 5

class MStream 
{
protected:
    uint32_t _size = 0;
    uint32_t _position = 0;
    uint8_t _load_address[2] = {0, 0};
    uint8_t _error = 0;

public:
    virtual ~MStream() {};

    std::ios_base::openmode mode;
    std::string url = "";

    bool has_subdirs = true;
    size_t block_size = 256;

    virtual std::unordered_map<std::string, std::string> info() {
        return {};
    }

    virtual uint32_t size() {
        return _size;
    };

    virtual uint32_t available() {
        if ( _position > _size )
            return 0;

        return _size - _position;
    };

    virtual uint32_t position() {
        return _position;
    }
    virtual bool position( uint32_t p) {
        _position = p;
        return seek( _position );
    }

    virtual size_t error() {
        return _error;
    }

    virtual bool eos()  {
        //Debug_printv("_size[%d] _available[%d] _position[%d]", _size, available(), _position);
        if ( available() <= 0 )
            return true;
        
        return false;
    }
    virtual void reset() 
    {
        _size = block_size;
        _position = 0;
    };
    
    virtual bool isOpen() = 0;
    virtual bool isBrowsable() { return false; };
    virtual bool isRandomAccess() { return false; };

    virtual bool open(std::ios_base::openmode mode) = 0;
    virtual void close() = 0;

    virtual uint32_t read(uint8_t* buf, uint32_t size) = 0;
    virtual uint32_t write(const uint8_t *buf, uint32_t size) = 0;

    virtual bool seek(uint32_t pos, int mode) {
        if(mode == SEEK_SET) {
            _position = pos;
        }
        else if(mode == SEEK_CUR) {
            _position = _position + pos;
        }
        else {
            _position = _size - pos;
        }
        return seek( _position );
    }
    virtual bool seek(uint32_t pos) = 0;

    // For files with a browsable random access directory structure
    // d64, d74, d81, dnp, etc.
    virtual bool seekPath(std::string path) {
        return false;
    };

    // For files with no directory structure
    // tap, crt, tar
    virtual std::string seekNextEntry() {
        return "";
    };

    virtual bool seekBlock( uint64_t index, uint8_t offset = 0 ) { return false; };
    virtual bool seekSector( uint8_t track, uint8_t sector, uint8_t offset = 0 ) { return false; };
    virtual bool seekSector( std::vector<uint8_t> trackSectorOffset ) { return false; };

private:

    // DEVICE
    friend class FlashMFile;

    // NETWORK
    friend class HTTPMFile;
    friend class TNFSMFile;

    // SERVICE
    friend class CSIPMFile;
    friend class TCPMFile;
};


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
    virtual MStream* createStream(std::ios_base::openmode) { return nullptr; };

    MFile* cd(std::string newDir);
    MFile* cdParent(std::string = "");
    MFile* cdLocalParent(std::string);
    MFile* cdRoot(std::string);
    MFile* cdLocalRoot(std::string);
    virtual bool isDirectory() = 0;
    virtual bool rewindDirectory() = 0 ;
    virtual MFile* getNextFileInDir() = 0 ;
    virtual bool mkDir() = 0 ;    
    virtual bool exists();
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
    *  @brief  Creates a unique_ptr<MFile> instance from a given MFile
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


/********************************************************
 * Utility implementations
 ********************************************************/

class FileBroker {
    static std::unordered_map<std::string, MFile*> file_repo;
public:
    template<class T> static T* obtain(std::string url, MFile* sourceFile) 
    {
        //Debug_printv("streams[%d] url[%s]", file_repo.size(), url.c_str());

        // obviously you have to supply STREAMFILE.url to this function!
        if(file_repo.find(url)!=file_repo.end())
        {
            Debug_printv("Reusing Existing MFile url[%s]", url.c_str());
            return (T*)file_repo.at(url);
        }

        // create and add stream to broker if not found
        Debug_printv("Creating New Stream url[%s]", url.c_str());
        auto newFile = MFSOwner::File(url);

        if ( newFile != nullptr )
        {
            // Are we at the root of the filesystem?
            if ( newFile->pathInStream == "")
            {
                Debug_printv("ROOT FILESYSTEM... CACHING [%s]", url.c_str());
                file_repo.insert(std::make_pair(url, newFile));
            }
            else
            {
                Debug_printv("SINGLE FILE... DON'T CACHE [%s]", url.c_str());
            }
            
            return newFile;
        }

        delete newFile;
        return nullptr;
    }

    static MFile* obtain(std::string url, MFile* sourceFile) {
        return obtain<MFile>(url, sourceFile);
    }

    static void dispose(std::string url) {
        if(file_repo.find(url)!=file_repo.end()) {
            auto toDelete = file_repo.at(url);
            file_repo.erase(url);
            delete toDelete;
        }
        Debug_printv("streams[%d]", file_repo.size());
    }

    static void validate() {
        
    }
};

class StreamBroker {
    static std::unordered_map<std::string, MStream*> stream_repo;
public:
    template<class T> static T* obtain(std::string url, std::ios_base::openmode mode) 
    {
        //Debug_printv("streams[%d] url[%s]", stream_repo.size(), url.c_str());

        // obviously you have to supply STREAMFILE.url to this function!
        if(stream_repo.find(url)!=stream_repo.end())
        {
            Debug_printv("Reusing Existing Stream url[%s]", url.c_str());
            return (T*)stream_repo.at(url);
        }

        // create and add stream to broker if not found
        Debug_printv("Creating New Stream url[%s]", url.c_str());
        auto newFile = MFSOwner::File(url);

        T* newStream = (T*)newFile->createStream(mode);

        if ( newStream != nullptr )
        {
            // Are we at the root of the filesystem?
            if ( newFile->pathInStream == "")
            {
                Debug_printv("ROOT FILESYSTEM... CACHING [%s]", url.c_str());
                stream_repo.insert(std::make_pair(url, newStream));
            }
            else
            {
                Debug_printv("SINGLE FILE... DON'T CACHE [%s]", url.c_str());
            }
            
            return newStream;
        }

        delete newFile;
        return nullptr;
    }

    static MStream* obtain(std::string url, std::ios_base::openmode mode) {
        return obtain<MStream>(url, mode);
    }

    static void dispose(std::string url) {
        if(stream_repo.find(url)!=stream_repo.end()) {
            auto toDelete = stream_repo.at(url);
            stream_repo.erase(url);
            delete toDelete;
        }
        Debug_printv("streams[%d]", stream_repo.size());
    }

    static void validate() {
        
    }
};
#endif // MEATLOAF_FILE
