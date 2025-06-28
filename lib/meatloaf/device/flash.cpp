#include "flash.h"

#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>

#include "../meatloaf.h"
#include "../../../include/debug.h"
#include "peoples_url_parser.h"
#include "string_utils.h"

/********************************************************
 * MFileSystem implementations
 ********************************************************/

bool FlashMFileSystem::handles(std::string path) 
{
    return true; // fallback fs, so it must be last on FS list
}

MFile* FlashMFileSystem::getFile(std::string path)
{
    //Debug_printv("path[%s]", path.c_str());
    return new FlashMFile(path);
}


/********************************************************
 * MFile implementations
 ********************************************************/

bool FlashMFile::pathValid(std::string path) 
{
    auto apath = std::string(basepath + path).c_str();
    while (*apath) {
        const char *slash = strchr(apath, '/');
        if (!slash) {
            if (strlen(apath) >= FILENAME_MAX) {
                // Terminal filename is too long
                return false;
            }
            break;
        }
        if ((slash - apath) >= FILENAME_MAX) {
            // This subdir name too long
            return false;
        }
        apath = slash + 1;
    }

    return true;
}

bool FlashMFile::isDirectory()
{
    if(path=="/" || path=="")
        return true;

    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);
    return S_ISDIR(info.st_mode);
}


MStream* FlashMFile::getSourceStream(std::ios_base::openmode mode)
{
    std::string full_path = basepath + path;
    MStream* istream = new FlashMStream(full_path, mode);
    //auto istream = StreamBroker::obtain<FlashMStream>(full_path, mode);
    //Debug_printv("FlashMFile::getSourceStream() 3, not null=%d", istream != nullptr);
    istream->open(mode);   
    //Debug_printv("FlashMFile::getSourceStream() 4");
    return istream;
}

MStream* FlashMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    return is.get(); // we don't have to process this stream in any way, just return the original stream
}

MStream* FlashMFile::createStream(std::ios_base::openmode mode)
{
    std::string full_path = basepath + path;
    MStream* istream = new FlashMStream(full_path, mode);
    return istream;
}

time_t FlashMFile::getLastWrite()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t FlashMFile::getCreationTime()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool FlashMFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir(std::string(basepath + path).c_str(), ALLPERMS);
    return (rc==0);
}

bool FlashMFile::rmDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = rmdir(std::string(basepath + path).c_str());
    return (rc==0);
}

bool FlashMFile::exists()
{
    if (m_isNull) {
        return false;
    }
    if (path=="/" || path=="") {
        return true;
    }

    //Debug_printv( "basepath[%s] path[%s]", basepath.c_str(), path.c_str() );

    struct stat st;
    int i = stat(std::string(basepath + path).c_str(), &st);

    return (i == 0);
}


bool FlashMFile::remove() {
    // musi obslugiwac usuwanie plikow i katalogow!
    if(path.empty())
        return false;

    int rc = ::remove( std::string(basepath + path).c_str() );
    if (rc != 0) {
        Debug_printv("remove: rc=%d path=`%s`\r\n", rc, path.c_str());
        return false;
    }

    return true;
}


bool FlashMFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    int rc = ::rename( std::string(basepath + path).c_str(), std::string(basepath + pathTo).c_str() );
    if (rc != 0) {
        return false;
    }
    return true;
}


void FlashMFile::openDir(std::string path) 
{
    if (!isDirectory()) { 
        dirOpened = false;
        return;
    }
    
    // Debug_printv("path[%s]", apath.c_str());
    if(path.empty()) {
        dir = opendir( "/" );
    }
    else {
        dir = opendir( path.c_str() );
    }

    dirOpened = true;
    if ( dir == NULL ) {
        dirOpened = false;
    }
    // else {
    //     // Skip the . and .. entries
    //     struct dirent* dirent = NULL;
    //     dirent = readdir( dir );
    //     dirent = readdir( dir );
    // }
}


void FlashMFile::closeDir() 
{
    if(dirOpened) {
        closedir( dir );
        dirOpened = false;
    }
}


bool FlashMFile::rewindDirectory()
{
    _valid = false;
    rewinddir( dir );

    // // Skip the . and .. entries
    // struct dirent* dirent = NULL;
    // dirent = readdir( dir );
    // dirent = readdir( dir );

    return (dir != NULL) ? true: false;
}


MFile* FlashMFile::getNextFileInDir()
{
    // Debug_printv("base[%s] path[%s]", basepath.c_str(), path.c_str());
    if(!dirOpened)
        openDir(std::string(basepath + path).c_str());

    if(dir == nullptr)
        return nullptr;

    // Debug_printv("before readdir(), dir not null:%d", dir != nullptr);
    struct dirent* dirent = NULL;
    do
    {
        dirent = readdir( dir );
    } while ( dirent != NULL && mstr::startsWith(dirent->d_name, ".") ); // Skip hidden files
    
    if ( dirent != NULL )
    {
        //Debug_printv("path[%s] name[%s]", this->path.c_str(), dirent->d_name);
        std::string entry_name = this->path + ((this->path == "/") ? "" : "/") + std::string(dirent->d_name);

        auto file = new FlashMFile(entry_name);
        file->extension = " " + file->extension;

        if(file->isDirectory()) {
            file->size = 0;
        }
        else {
            struct stat info;
            stat( std::string(entry_name).c_str(), &info);
            file->size = info.st_size;
        }

        return file;
    }
    else
    {
        closeDir();
        return nullptr;
    }
}


bool FlashMFile::readEntry( std::string filename )
{
    std::string apath = (basepath + pathToFile()).c_str();
    if (apath.empty()) {
        apath = "/";
    }

    Debug_printv( "path[%s] filename[%s] size[%d]", apath.c_str(), filename.c_str(), filename.size());

    DIR* d = opendir( apath.c_str() );
    if(d == nullptr)
        return false;

    // Read Directory Entries
    if ( filename.size() > 0 )
    {
        struct dirent* dirent = NULL;
        bool found = false;
        bool wildcard =  ( mstr::contains(filename, "*") || mstr::contains(filename, "?") );
        while ( (dirent = readdir( d )) != NULL )
        {
            std::string entryFilename = dirent->d_name;

            Debug_printv("path[%s] filename[%s] entry.filename[%.16s]", apath.c_str(), filename.c_str(), entryFilename.c_str());

            // Read Entry From Stream
            if ( dirent->d_type != DT_DIR ) // Only want to match files not directories
            {
                // Read Entry From Stream
                if ( filename == entryFilename ) // Match exact
                {
                    found = true;
                }
                else if ( wildcard )
                {
                    if (filename == "*") // Match first entry
                    {
                        filename = entryFilename;
                        found = true;
                    }
                    else if ( mstr::compare(filename, entryFilename) ) // X?XX?X* Wildcard match
                    {
                        // Set filename to this filename
                        Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
                        resetURL(apath + "/" + entryFilename);
                        found = true;
                    }
                }

                if ( found )
                {
                    _exists = true;
                    closedir( d );
                    return true;
                }
            }
        }

        Debug_printv( "Not Found! file[%s]", filename.c_str() );
    }

    closedir( d );
    return false;
}



/********************************************************
 * MStream implementations
 ********************************************************/

bool FlashMStream::open(std::ios_base::openmode mode) {
    if(isOpen())
        return true;

    //Debug_printv("IStream: trying to open flash fs, calling isOpen");

    //Debug_printv("IStream: wasn't open, calling obtain");
    if(mode == std::ios_base::in)
        handle->obtain(localPath, "r");
    else if(mode == std::ios_base::out) {
        Debug_printv("FlashMStream: ok, we are in write mode!");
        handle->obtain(localPath, "w");
    }
    else if(mode == std::ios_base::app)
        handle->obtain(localPath, "a");
    else if(mode == (std::ios_base::in | std::ios_base::out))
        handle->obtain(localPath, "r+");
    else if(mode == (std::ios_base::in | std::ios_base::app))
        handle->obtain(localPath, "a+");
    else if(mode == (std::ios_base::in | std::ios_base::out | std::ios_base::trunc))
        handle->obtain(localPath, "w+");
    else if(mode == (std::ios_base::in | std::ios_base::out | std::ios_base::app))
        handle->obtain(localPath, "a+");

    // The below code will definitely destroy whatever open above does, because it will move the file pointer
    // so I just wrapped it to be called only for in
    if( isOpen() && ((mode==std::ios_base::in) || (mode==(std::ios_base::in|std::ios_base::out)))  ) {
        //Debug_printv("IStream: past obtain");
        // Set file size
        fseek(handle->file_h, 0, SEEK_END);
        //Debug_printv("IStream: past fseek 1");
        _size = ftell(handle->file_h);
        _position = 0;
        //Debug_printv("IStream: past ftell");
        fseek(handle->file_h, 0, SEEK_SET);
        //Debug_printv("IStream: past fseek 2");
        return true;
    }
    return false;
};

void FlashMStream::close() {
    if(isOpen()) handle->dispose();
};

uint32_t FlashMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen() || !buf) {
        Debug_printv("Not open");
        return 0;
    }

    uint32_t bytesRead = 0;
    
    if ( size > 0 )
    {
        if ( size > available() )
            size = available();

        bytesRead = fread((void*) buf, 1, size, handle->file_h );
        // Debug_printv("bytesRead[%d]", bytesRead);
        // auto hex = mstr::toHex(buf, bytesRead);
        // Debug_printv("[%s]", hex.c_str());
        _position += bytesRead;
    }

    return bytesRead;
};

uint32_t FlashMStream::write(const uint8_t *buf, uint32_t size) {
    if (!isOpen() || !buf) {
        Debug_printv("Not open");
        return 0;
    }

    //Debug_printv("buf[%02X] size[%ld]", buf[0], size);

    // buffer, element size, count, handle
    int result = fwrite((void*) buf, 1, size, handle->file_h );

    //Debug_printv("result[%d]", result);
    return result;
};


bool FlashMStream::seek(uint32_t pos) {
    // Debug_printv("pos[%d]", pos);
    if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    _position = pos;
    return ( fseek( handle->file_h, pos, SEEK_SET ) ) ? false : true;
};

bool FlashMStream::isOpen() {
    // Debug_printv("Inside isOpen, handle notnull:%d", handle != nullptr);
    auto temp = handle != nullptr && handle->file_h != nullptr;
    // Debug_printv("returning");
    return temp;
}

/********************************************************
 * FlashHandle implementations
 ********************************************************/


FlashHandle::~FlashHandle() {
    dispose();
}

void FlashHandle::dispose() {
    //Debug_printv("file_h[%d]", file_h);
    if (file_h != nullptr) {

        fclose( file_h );
        file_h = nullptr;
        // rc = -255;
    }
}

void FlashHandle::obtain(std::string m_path, std::string mode) {

    //printf("*** Atempting opening flash  handle'%s'\r\n", m_path.c_str());

    if ((mode[0] == 'w') && strchr(m_path.c_str(), '/')) {
        // For file creation, silently make subdirs as needed.  If any fail,
        // it will be caught by the real file open later on

        char *pathStr = new char[m_path.length()];
        strncpy(pathStr, m_path.data(), m_path.length());

        if (pathStr) {
            // Make dirs up to the final fnamepart
            char *ptr = strchr(pathStr, '/');
            while (ptr) {
                *ptr = 0;
                mkdir(pathStr, ALLPERMS);
                *ptr = '/';
                ptr = strchr(ptr+1, '/');
            }
        }
        delete[] pathStr;
    }

    //Debug_printv("m_path[%s] mode[%s]", m_path.c_str(), mode.c_str());
    file_h = fopen( m_path.c_str(), mode.c_str());
    // rc = 1;

    //printf("FSTEST: lfs_file_open file rc:%d\r\n",rc);

//     if (rc == LFS_ERR_ISDIR) {
//         // To support the SD.openNextFile, a null FD indicates to the FlashFSFile this is just
//         // a directory whose name we are carrying around but which cannot be read or written
//     } else if (rc == 0) {
// //        lfs_file_sync(&FlashMFileSystem::lfsStruct, &file_h);
//     } else {
//         Debug_printv("FlashMFile::open: unknown return code rc=%d path=`%s`\r\n",
//                rc, m_path.c_str());
//     }
}
