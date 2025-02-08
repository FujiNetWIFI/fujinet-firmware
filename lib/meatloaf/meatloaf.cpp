#include "meatloaf.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <sstream>

std::unordered_map<std::string, MFile*> FileBroker::file_repo;
std::unordered_map<std::string, MStream*> StreamBroker::stream_repo;

#ifdef FLASH_SPIFFS
#include "esp_spiffs.h"
#endif

#ifdef FLASH_LITTLEFS
#include "esp_littlefs.h"
#endif


//#include "meat_broker.h"
#include "meat_buffer.h"
//#include "wrappers/directory_stream.h"

#include "string_utils.h"
#include "peoples_url_parser.h"

#include "MIOException.h"
#include "../../include/debug.h"

// Archive
//#include "archive/archive_ml.h"

// Cartridge

// Container
#include "container/d8b.h"
#include "container/dfi.h"

// Device
#include "device/flash.h"
#include "device/sd.h"

// Disk
#include "disk/d64.h"
#include "disk/d71.h"
#include "disk/d80.h"
#include "disk/d81.h"
#include "disk/d82.h"
#include "disk/d90.h"
#include "disk/dnp.h"

// File
#include "file/p00.h"

// Link
// Loaders

// Network
#include "network/http.h"
#include "network/tnfs.h"
// #include "network/ipfs.h"
// #include "network/smb.h"
// #include "network/ws.h"

// Scanners

// Service
#include "service/csip.h"
#include "service/ml.h"

// Tape
#include "tape/t64.h"
#include "tape/tcrt.h"

//std::unordered_map<std::string, MFile*> FileBroker::file_repo;
//std::unordered_map<std::string, MStream*> StreamBroker::stream_repo;

/********************************************************
 * MFSOwner implementations
 ********************************************************/

// initialize other filesystems here
FlashMFileSystem defaultFS;
#ifdef SD_CARD
SDFileSystem sdFS;
#endif


// Archive
//ArchiveMFileSystem archiveFS;

// Cartridge

// Container
D8BMFileSystem d8bFS;
DFIMFileSystem dfiFS;

// File
P00MFileSystem p00FS;

// Disk
D64MFileSystem d64FS;
D71MFileSystem d71FS;
D80MFileSystem d80FS;
D81MFileSystem d81FS;
D82MFileSystem d82FS;
D90MFileSystem d90FS;
DNPMFileSystem dnpFS;

// Network
HTTPMFileSystem httpFS;
TNFSMFileSystem tnfsFS;
// IPFSFileSystem ipfsFS;
// TcpFileSystem tcpFS;
//WSFileSystem wsFS;

// Service
CSIPMFileSystem csipFS;
MLMFileSystem mlFS;

// Tape
T64MFileSystem t64FS;
TCRTMFileSystem tcrtFS;


// put all available filesystems in this array - first matching system gets the file!
// fist in list is default
std::vector<MFileSystem*> MFSOwner::availableFS { 
    &defaultFS,
#ifdef SD_CARD
    &sdFS,
#endif
//    &archiveFS, // extension-based FS have to be on top to be picked first, otherwise the scheme will pick them!
    &d64FS, &d71FS, &d80FS, &d81FS, &d82FS, &d90FS, &dnpFS,
    &d8bFS, &dfiFS,
    &p00FS,
    &httpFS, &tnfsFS,
    &csipFS, &mlFS,
    &t64FS, &tcrtFS
//    &ipfsFS, &tcpFS,
//    &tnfsFS
};

bool MFSOwner::mount(std::string name) {
    Debug_print("MFSOwner::mount fs:");
    Debug_println(name.c_str());

    for(auto i = availableFS.begin() + 1; i < availableFS.end() ; i ++) {
        auto fs = (*i);

        if(fs->handles(name)) {
                Debug_printv("MFSOwner found a proper fs");

            bool ok = fs->mount();

            if(ok)
                Debug_print("Mounted fs: ");
            else
                Debug_print("Couldn't mount fs: ");

            Debug_println(name.c_str());

            return ok;
        }
    }
    return false;
}

bool MFSOwner::umount(std::string name) {
    for(auto i = availableFS.begin() + 1; i < availableFS.end() ; i ++) {
        auto fs = (*i);

        if(fs->handles(name)) {
            return fs->umount();
        }
    }
    return true;
}

MFile* MFSOwner::File(MFile* file) {
    return File(file->url);
}

MFile* MFSOwner::File(std::shared_ptr<MFile> file) {
    return File(file->url);
}


MFile* MFSOwner::File(std::string path) {
    // if(mstr::startsWith(path,"cs:", false)) {
    //     //printf("CServer path found!\r\n");
    //     return csFS.getFile(path);
    // }

    std::vector<std::string> paths = mstr::split(path,'/');

    //Debug_printv("Trying to factory path [%s]", path.c_str());

    auto pathIterator = paths.end();
    auto begin = paths.begin();
    auto end = paths.end();

    auto foundFS = testScan(begin, end, pathIterator);

    if ( foundFS != nullptr )
    {
        //Debug_printv("PATH: '%s' is in FS [%s]", path.c_str(), foundFS->symbol);
        auto newFile = foundFS->getFile(path);
        //Debug_printv("newFile: '%s'", newFile->url.c_str());

        pathIterator++;
        newFile->pathInStream = mstr::joinToString(&pathIterator, &end, "/");
        //Debug_printv("newFile->pathInStream: '%s'", newFile->pathInStream.c_str());

        auto endHere = pathIterator;
        pathIterator--;

        if(begin == pathIterator) 
        {
            //Debug_printv("** LOOK DOWN PATH NOT NEEDED   path[%s]", path.c_str());
            newFile->streamFile = foundFS->getFile(mstr::joinToString(&begin, &pathIterator, "/"));
        } 
        else 
        {
            auto upperPath = mstr::joinToString(&begin, &pathIterator, "/");
            //Debug_printv("** LOOK DOWN PATH: %s", upperPath.c_str());

            auto upperFS = testScan(begin, end, pathIterator);

            if ( upperFS != nullptr )
            {
                auto wholePath = mstr::joinToString(&begin, &endHere, "/");

                //auto cp = mstr::joinToString(&begin, &pathIterator, "/");
                //Debug_printv("CONTAINER PATH WILL BE: '%s' ", wholePath.c_str());
                newFile->streamFile = upperFS->getFile(wholePath); // skończy się na d64
                //Debug_printv("CONTAINER: '%s' is in FS [%s]", newFile->streamFile->url.c_str(), upperFS->symbol);
            }
            else
            {
                //Debug_printv("WARNING!!!! CONTAINER FAILED FOR: '%s'", upperPath.c_str());
            }
        }

        return newFile;
    }

    Debug_printv("Not Found! path[%s]", path.c_str());

    return nullptr;
}

MFile* MFSOwner::NewFile(std::string path) {

    auto newFile = File(path);
    if ( newFile != nullptr )
        return nullptr;
    
    if (newFile->exists()) {
        Debug_printv("File already exists [%s]", path.c_str());
        return nullptr;
    }



    return newFile;
}


std::string MFSOwner::existsLocal( std::string path )
{
    auto url = PeoplesUrlParser::parseURL( path );

    // Debug_printv( "path[%s] name[%s] size[%d]", path.c_str(), url.name.c_str(), url.name.size() );
    if ( url->name.size() == 16 )
    {
        struct stat st;
        int i = stat(std::string(path).c_str(), &st);

        // If not found try for a wildcard match
        if ( i == -1 )
        {
            DIR *dir;
            struct dirent *ent;

            std::string p = url->pathToFile();
            std::string name = url->name;
            // Debug_printv( "pathToFile[%s] basename[%s]", p.c_str(), name.c_str() );
            if ((dir = opendir ( p.c_str() )) != NULL)
            {
                /* print all the files and directories within directory */
                std::string e;
                while ((ent = readdir (dir)) != NULL) {
                    // Debug_printv( "%s\r\n", ent->d_name );
                    e = ent->d_name;
                    if ( mstr::compare( name, e ) )
                    {
                        path = ( p + "/" + e );
                        break;
                    }
                }
                closedir (dir);
            }
        }        
    }

    return path;
}

MFileSystem* MFSOwner::testScan(std::vector<std::string>::iterator &begin, std::vector<std::string>::iterator &end, std::vector<std::string>::iterator &pathIterator) {
    while (pathIterator != begin) {
        pathIterator--;

        auto part = *pathIterator;
        mstr::toLower(part);

        //Debug_printv("index[%d] pathIterator[%s] size[%d]", pathIterator, pathIterator->c_str(), pathIterator->size());

        auto foundIter=std::find_if(availableFS.begin() + 1, availableFS.end(), [&part](MFileSystem* fs){ 
            //Debug_printv("symbol[%s]", fs->symbol);
            return fs->handles(part); 
        } );

        if(foundIter != availableFS.end()) {
            //Debug_printv("matched part '%s'\r\n", part.c_str());
            return (*foundIter);
        }
    };

    auto fs = *availableFS.begin();
    //Debug_printv("return default file system [%s]", fs->symbol);
    return fs;
}

/********************************************************
 * MFileSystem implementations
 ********************************************************/

MFileSystem::MFileSystem(const char* s)
{
    symbol = s;
}

MFileSystem::~MFileSystem() {}

/********************************************************
 * MFile implementations
 ********************************************************/

MFile::MFile(std::string path) {
    // Debug_printv("path[%s]", path.c_str());

    // if ( mstr::contains(path, "$") )
    // {
    //     // Create directory stream here
    //     Debug_printv("Create directory stream here!");
    //     path = "";
    // }
    //Debug_printv("ctor path[%s]", path.c_str());

    resetURL(path);
}

MFile::MFile(std::string path, std::string name) : MFile(path + "/" + name) {
    if(mstr::startsWith(name, "xn--")) {
        this->path = path + "/" + U8Char::fromPunycode(name);
    }
}

MFile::MFile(MFile* path, std::string name) : MFile(path->path + "/" + name) {
    if(mstr::startsWith(name, "xn--")) {
        this->path = path->path + "/" + U8Char::fromPunycode(name);
    }
}

bool MFile::operator!=(nullptr_t ptr) {
    return m_isNull;
}

MStream* MFile::getSourceStream(std::ios_base::openmode mode) {

    if ( streamFile == nullptr )
    {
        Debug_printv("null streamFile");
        return nullptr;
    }

    // has to return OPENED stream
    //Debug_printv("pathInStream[%s] streamFile[%s]", pathInStream.c_str(), streamFile->url.c_str());

    auto sourceStream = streamFile->getSourceStream(mode);
    if ( sourceStream == nullptr )
    {
        Debug_printv("null sourceStream");
        return nullptr;
    }

    // will be replaced by streamBroker->getSourceStream(streamFile, mode)
    std::shared_ptr<MStream> containerStream(sourceStream); // get its base stream, i.e. zip raw file contents

    //Debug_printv("containerStream isRandomAccess[%d] isBrowsable[%d]", containerStream->isRandomAccess(), containerStream->isBrowsable());

    // will be replaced by streamBroker->getDecodedStream(this, mode, containerStream)
    MStream* decodedStream(getDecodedStream(containerStream)); // wrap this stream into decoded stream, i.e. unpacked zip files
    decodedStream->url = this->url;
    //Debug_printv("decodedStream isRandomAccess[%d] isBrowsable[%d]", decodedStream->isRandomAccess(), decodedStream->isBrowsable());

    //Debug_printv("pathInStream [%s]", pathInStream.c_str());

    if(decodedStream->isRandomAccess() && pathInStream != "")
    {
        // For files with a browsable random access directory structure
        // d64, d74, d81, dnp, etc.
        bool foundIt = decodedStream->seekPath(this->pathInStream);

        if(!foundIt)
        {
            //Debug_printv("path in stream not found");
            return nullptr;
        }        
    }
    else if(decodedStream->isBrowsable() && pathInStream != "")
    {
        // For files with no directory structure
        // tap, crt, tar
        auto pointedFile = decodedStream->seekNextEntry();

        while (!pointedFile.empty())
        {
            if(mstr::compare(this->pathInStream, pointedFile))
            {
                //Debug_printv("returning decodedStream 1");
                return decodedStream;
            }

            pointedFile = decodedStream->seekNextEntry();
        }
        //Debug_printv("path in stream not found!");
        if(pointedFile.empty())
            return nullptr;
    }

    //Debug_printv("returning decodedStream 2");
    return decodedStream;
};

bool MFile::format(std::string header, std::string id)
{
    // Open the file in write mode
    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);

    if (fd == -1) {
        // Handle file opening error
        return false;
    }

    // Truncate the file to the desired size
    if (ftruncate(fd, size) == -1) {
        // Handle file truncation error
        close(fd);
        return false;
    }

    // Write the header to the file

    // Clear directory track


    close(fd);
    return true;
}


MFile* MFile::cd(std::string newDir) 
{
    Debug_printv("cd[%s]", newDir.c_str());

    // OK to clarify - coming here there should be ONLY path or magicSymbol-path combo!
    // NO "cd:xxxxx", no "/cd:xxxxx" ALLOWED here! ******************
    //
    // if you want to support LOAD"CDxxxxxx" just parse/drop the CD BEFORE calling this function
    // and call it ONLY with the path you want to change into!

    if(newDir.find(':') != std::string::npos) 
    {
        // I can only guess we're CDing into another url scheme, this means we're changing whole path
        return MFSOwner::File(newDir);
    }
    else if(newDir[0]=='_') // {CBM LEFT ARROW}
    {
        // user entered: CD:_ or CD_ 
        // means: go up one directory

        // user entered: CD:_DIR or CD_DIR
        // means: go to a directory in the same directory as this one
        return cdParent(mstr::drop(newDir,1));
    }
    else if(newDir[0]=='.' && newDir[1]=='.')
    {
        if(newDir.size()==2) 
        {
            // user entered: CD:.. or CD..
            // means: go up one directory
            return cdParent();
        }
        else 
        {
            // user entered: CD:..DIR or CD..DIR
            // meaning: Go back one directory
            return cdLocalParent(mstr::drop(newDir,2));
        }
    }
    else if(newDir[0]=='/' && newDir[1]=='/') 
    {
        // user entered: CD:// or CD//
        // means: change to the root of stream

        // user entered: CD://DIR or CD//DIR
        // means: change to a dir in root of stream
        return cdLocalRoot(mstr::drop(newDir,2));
    }
    else if(newDir[0]=='/') 
    {
        // user entered: CD:/DIR or CD/DIR
        // means: go to a directory in the same directory as this one
        return cdParent(mstr::drop(newDir,1));
    }
    else if(newDir[0]=='^') // {CBM UP ARROW}
    {
        // user entered: CD:^ or CD^ 
        // means: change to flash root
        return cdRoot(mstr::drop(newDir,1));
    }
    else 
    {
        //newDir = mstr::toUTF8( newDir );

        // Add new directory to path
        if ( !mstr::endsWith(url, "/") && newDir.size() )
            url.push_back('/');

        // Add new directory to path
        MFile* newPath = MFSOwner::File(url + newDir);

        if(mstr::endsWith(newDir, ".url", false)) {
            // we need to get actual url

            //auto reader = Meat::New<MFile>(newDir);
            //auto istream = reader->getSourceStream();
            Meat::iostream reader(newPath);

            //uint8_t url[istream->size()]; // NOPE, streams have no size!
            //istream->read(url, istream->size());
            std::string url;
            reader >> url;

            Debug_printv("url[%s]", url.c_str());
            //std::string ml_url((char *)url);

            delete newPath;
            newPath = MFSOwner::File(url);
        }

        return newPath;
    }

    return nullptr;
};


MFile* MFile::cdParent(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());

    // drop last dir
    // add plus
    if(path.empty()) 
    {
        // from here we can go only to flash root!
        return MFSOwner::File("/");
    }
    else 
    {
        int lastSlash = url.find_last_of('/');
        if ( lastSlash == url.size() - 1 ) 
        {
            lastSlash = url.find_last_of('/', url.size() - 2);
        }
        std::string newDir = mstr::dropLast(url, url.size() - lastSlash);
        if(!plus.empty())
            newDir+= ("/" + plus);

        return MFSOwner::File(newDir);
    }
};

MFile* MFile::cdLocalParent(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    // drop last dir
    // check if it isn't shorter than streamFile
    // add plus
    int lastSlash = url.find_last_of('/');
    if ( lastSlash == url.size() - 1 ) {
        lastSlash = url.find_last_of('/', url.size() - 2);
    }
    std::string parent = mstr::dropLast(url, url.size() - lastSlash);
    if(parent.length()-streamFile->url.length()>1)
        parent = streamFile->url;
    return MFSOwner::File( parent + "/" + plus );
};

MFile* MFile::cdRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());
    return MFSOwner::File( "/" + plus );
};

MFile* MFile::cdLocalRoot(std::string plus) 
{
    Debug_printv("url[%s] path[%s] plus[%s]", url.c_str(), path.c_str(), plus.c_str());

    if ( path.empty() || streamFile == nullptr ) {
        // from here we can go only to flash root!
        return MFSOwner::File( "/" + plus );
    }
    return MFSOwner::File( streamFile->url + "/" + plus );
};

// bool MFile::copyTo(MFile* dst) {
//     Debug_printv("in copyTo\r\n");
//     Meat::iostream istream(this);
//     Meat::iostream ostream(dst);

//     int rc;

//     Debug_printv("in copyTo, iopen=%d oopen=%d\r\n", istream.is_open(), ostream.is_open());

//     if(!istream.is_open() || !ostream.is_open())
//         return false;

//     Debug_printv("commencing copy\r\n");

//     while((rc = istream.get())!= EOF) {     
//         ostream.put(rc);
//         if(ostream.bad() || istream.bad())
//             return false;
//     }

//     Debug_printv("copying finished, rc=%d\r\n", rc);

//     return true;
// };

bool MFile::exists() { 
    Debug_printv("here!");
    return _exists; 
};

uint64_t MFile::getAvailableSpace()
{
    if ( mstr::startsWith(path, (char *)"/sd") )
    {
#ifdef SD_CARD
        FATFS* fsinfo;
        DWORD fre_clust;

        if (f_getfree("/", &fre_clust, &fsinfo) == 0)
        {
            uint64_t total = ((uint64_t)(fsinfo->csize)) * (fsinfo->n_fatent - 2) * (fsinfo->ssize);
            uint64_t used = ((uint64_t)(fsinfo->csize)) * ((fsinfo->n_fatent - 2) - (fsinfo->free_clst)) * (fsinfo->ssize);
            uint64_t free = total - used;
            //Debug_printv("total[%llu] used[%llu free[%llu]", total, used, free);
            return free;
        }
#endif
    }
    else
    {
        size_t total = 0, used = 0;
#ifdef FLASH_SPIFFS
        esp_spiffs_info("flash", &total, &used);
#elif FLASH_LITTLEFS
        esp_littlefs_info("flash", &total, &used);
#endif
        size_t free = total - used;
        //Debug_printv("total[%d] used[%d] free[%d]", total, used, free);
        return free;
    }

    return 65535;
}
