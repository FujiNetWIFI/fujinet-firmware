#include "tnfs.h"

#include "meatloaf.h"

#include "../../../include/debug.h"

#include <sys/stat.h>
#include <unistd.h>



/********************************************************
 * MFile implementations
 ********************************************************/

bool TNFSMFile::pathValid(std::string path) 
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

bool TNFSMFile::isDirectory()
{
    if(path=="/" || path=="")
        return true;

    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);
    return S_ISDIR(info.st_mode);
}

MStream* TNFSMFile::getSourceStream(std::ios_base::openmode mode)
{
    std::string full_path = basepath + path;
    MStream* istream = new TNFSMStream(full_path);
    //auto istream = StreamBroker::obtain<TNFSMStream>(full_path, mode);
    //Debug_printv("TNFSMFile::getSourceStream() 3, not null=%d", istream != nullptr);
    istream->open(mode);   
    //Debug_printv("TNFSMFile::getSourceStream() 4");
    return istream;
}

MStream* TNFSMFile::getDecodedStream(std::shared_ptr<MStream> is) {
    return is.get(); // we don't have to process this stream in any way, just return the original stream
}

MStream* TNFSMFile::createStream(std::ios_base::openmode mode)
{
    std::string full_path = basepath + path;
    MStream* istream = new TNFSMStream(full_path);
    istream->open(mode);
    return istream;
}

time_t TNFSMFile::getLastWrite()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_mtime; // Time of last modification
    return ftime;
}

time_t TNFSMFile::getCreationTime()
{
    struct stat info;
    stat( std::string(basepath + path).c_str(), &info);

    time_t ftime = info.st_ctime; // Time of last status change
    return ftime;
}

bool TNFSMFile::mkDir()
{
    if (m_isNull) {
        return false;
    }
    int rc = mkdir(std::string(basepath + path).c_str(), ALLPERMS);
    return (rc==0);
}

bool TNFSMFile::exists()
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


bool TNFSMFile::remove() {
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


bool TNFSMFile::rename(std::string pathTo) {
    if(pathTo.empty())
        return false;

    int rc = ::rename( std::string(basepath + path).c_str(), std::string(basepath + pathTo).c_str() );
    if (rc != 0) {
        return false;
    }
    return true;
}


void TNFSMFile::openDir(std::string apath) 
{
    if (!isDirectory()) { 
        dirOpened = false;
        return;
    }
    
    // Debug_printv("path[%s]", apath.c_str());
    if(apath.empty()) {
        dir = opendir( "/" );
    }
    else {
        dir = opendir( apath.c_str() );
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


void TNFSMFile::closeDir() 
{
    if(dirOpened) {
        closedir( dir );
        dirOpened = false;
    }
}


bool TNFSMFile::rewindDirectory()
{
    _valid = false;
    rewinddir( dir );

    // // Skip the . and .. entries
    // struct dirent* dirent = NULL;
    // dirent = readdir( dir );
    // dirent = readdir( dir );

    return (dir != NULL) ? true: false;
}


MFile* TNFSMFile::getNextFileInDir()
{
    // Debug_printv("base[%s] path[%s]", basepath.c_str(), path.c_str());
    if(!dirOpened)
        openDir(std::string(basepath + path).c_str());

    if(dir == nullptr)
        return nullptr;

    struct dirent* dirent = NULL;
    do
    {
        dirent = readdir( dir );
    } while ( dirent != NULL && mstr::startsWith(dirent->d_name, ".") ); // Skip hidden files

    if ( dirent != NULL )
    {
        //Debug_printv("path[%s] name[%s]", this->path.c_str(), dirent->d_name);
        std::string entry_name = this->path + ((this->path == "/") ? "" : "/") + std::string(dirent->d_name);

        auto file = new TNFSMFile(entry_name);
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


bool TNFSMFile::readEntry( std::string filename )
{
    DIR* d;
    std::string apath = (basepath + pathToFile()).c_str();
    if (apath.empty()) {
        apath = "/";
    }

    Debug_printv( "path[%s] filename[%s] size[%d]", apath.c_str(), filename.c_str(), filename.size());

    d = opendir( apath.c_str() );
    if(d == nullptr)
        return false;

    // Read Directory Entries
    struct dirent* dirent = NULL;
    if ( filename.size() > 0 )
    {
        while ( (dirent = readdir( d )) != NULL )
        {
            std::string entryFilename = dirent->d_name;

            Debug_printv("path[%s] filename[%s] entry.filename[%.16s]", apath.c_str(), filename.c_str(), entryFilename.c_str());

            // Read Entry From Stream
            if (filename == "*")
            {
                filename = entryFilename;
                closedir( d );
                return true;
            }
            else if ( filename == entryFilename )
            {
                closedir( d );
                return true;
            }
            else if ( mstr::compare(filename, entryFilename) )
            {
                // Set filename to this filename
                Debug_printv( "Found! file[%s] -> entry[%s]", filename.c_str(), entryFilename.c_str() );
                resetURL(apath + "/" + std::string(dirent->d_name));
                closedir( d );
                return true;
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
uint32_t TNFSMStream::write(const uint8_t *buf, uint32_t size) {
    if (!isOpen() || !buf) {
        return 0;
    }

    //Debug_printv("in byteWrite '%c', handle->file_h is null=[%d]\r\n", buf[0], handle->file_h == nullptr);

    // buffer, element size, count, handle
    int result = fwrite((void*) buf, 1, size, handle->file_h );

    //Debug_printv("after lfs_file_write");

    if (result < 0) {
        Debug_printv("write rc=%d\r\n", result);
    }
    return result;
};


/********************************************************
 * MIStreams implementations
 ********************************************************/


bool TNFSMStream::open(std::ios_base::openmode mode) {
    if(isOpen())
        return true;

    //Debug_printv("IStream: trying to open flash fs, calling isOpen");

    //Debug_printv("IStream: wasn't open, calling obtain");
    handle->obtain(localPath, "r");

    if(isOpen()) {
        //Debug_printv("IStream: past obtain");
        // Set file size
        fseek(handle->file_h, 0, SEEK_END);
        //Debug_printv("IStream: past fseek 1");
        _size = ftell(handle->file_h);
        //Debug_printv("IStream: past ftell");
        fseek(handle->file_h, 0, SEEK_SET);
        //Debug_printv("IStream: past fseek 2");
        return true;
    }
    return false;
};

void TNFSMStream::close() {
    if(isOpen()) handle->dispose();
};

uint32_t TNFSMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen() || !buf) {
        Debug_printv("Not open");
        return 0;
    }

    int bytesRead = fread((void*) buf, 1, size, handle->file_h );

    if (bytesRead < 0) {
        Debug_printv("read rc=%d\r\n", bytesRead);
        return 0;
    }

    return bytesRead;
};

bool TNFSMStream::seek(uint32_t pos) {
    // Debug_printv("pos[%d]", pos);
        if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->file_h, pos, SEEK_SET ) ) ? true : false;
};

bool TNFSMStream::seek(uint32_t pos, int mode) {
    // Debug_printv("pos[%d] mode[%d]", pos, mode);
    if (!isOpen()) {
        Debug_printv("Not open");
        return false;
    }
    return ( fseek( handle->file_h, pos, mode ) ) ? true: false;
}

bool TNFSMStream::isOpen() {
    // Debug_printv("Inside isOpen, handle notnull:%d", handle != nullptr);
    auto temp = handle != nullptr && handle->file_h != nullptr;
    // Debug_printv("returning");
    return temp;
}

/********************************************************
 * TNFSHandle implementations
 ********************************************************/


TNFSHandle::~TNFSHandle() {
    dispose();
}

void TNFSHandle::dispose() {
    //Debug_printv("file_h[%d]", file_h);
    if (file_h != nullptr) {

        fclose( file_h );
        file_h = nullptr;
        // rc = -255;
    }
}

void TNFSHandle::obtain(std::string m_path, std::string mode) {

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
// //        lfs_file_sync(&TNFSMFileSystem::lfsStruct, &file_h);
//     } else {
//         Debug_printv("TNFSMFile::open: unknown return code rc=%d path=`%s`\r\n",
//                rc, m_path.c_str());
//     }
}
