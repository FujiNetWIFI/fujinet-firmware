
#include "fnFsHTTP.h"

#include <ctime>
#include <iostream>
#include <iomanip>
#include <sstream>

#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFileCache.h"
#include "string_utils.h"

// http timeout in ms
#define HTTP_GET_TIMEOUT 20000

#define COPY_BLK_SIZE 4096


#ifdef ESP_PLATFORM
#define HEAP_DEBUG() Debug_printv("free heap/low: %lu/%lu", esp_get_free_heap_size(), esp_get_free_internal_heap_size())
#else
#define HEAP_DEBUG()
#endif

FileSystemHTTP::FileSystemHTTP()
{
    Debug_printf("FileSystemHTTP::ctor\n");
    _http = nullptr;
    _url = nullptr;
    // invalidate _last_dir
    _last_dir[0] = '\0';
}

FileSystemHTTP::~FileSystemHTTP()
{
    Debug_printf("FileSystemHTTP::dtor\n");
    if (_started)
    {
        _dircache.clear();
//        _http->logout();
    }
    if (_http != nullptr)
        delete _http;
}

bool FileSystemHTTP::start(const char *url, const char *user, const char *password)
{
    if (_started)
        return false;

    if(url == nullptr || url[0] == '\0')
        return false;

    if (_http != nullptr)
    {
        delete _http;
        _http = nullptr;
    }

    // _http = new HTTP_CLIENT_CLASS();
    // if (_http == nullptr)
    // {
    //     Debug_println("FileSystemHTTP::start() - failed to create HTTP client\n");
    //     return false;
    // }

    _url = PeoplesUrlParser::parseURL(url);
    if (!_url->isValidUrl())
    {
        Debug_printf("FileSystemHTTP::start() - failed to parse URL \"%s\"\n", _url->url.c_str());
        return false;
    }

    Debug_println("FileSystemHTTP started");

    return _started = true;
}

bool FileSystemHTTP::exists(const char *path)
{
    // TODO
    return false;
}

bool FileSystemHTTP::remove(const char *path)
{
    return false;
}

bool FileSystemHTTP::rename(const char *pathFrom, const char *pathTo)
{
    return false;
}

FILE  *FileSystemHTTP::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemHTTP::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemHTTP::filehandler_open(const char *path, const char *mode)
{
    FileHandler *fh = cache_file(path, mode);
    return fh;
}

// Read file from HTTP path and write it to cache file
// Return FileHandler* on success (memory or SD file), nullptr on error
FileHandler *FileSystemHTTP::cache_file(const char *path, const char *mode)
{
    // Try SD cache first
    FileHandler *fh = FileCache::open(_url->mRawUrl.c_str(), path, mode);
    if (fh != nullptr)
        return fh; // cache hit, done

    HEAP_DEBUG();

    // Create new cache file (starts in memory)
    fc_handle *fc = FileCache::create(_url->mRawUrl.c_str(), path);
    if (fc == nullptr)
        return nullptr;

    // Setup HTTP client
    if (_http != nullptr)
        delete _http;
    _http = new HTTP_CLIENT_CLASS();
    if (_http == nullptr)
    {
        Debug_println("FileSystemHTTP::cache_file() - failed to create HTTP client\n");
        return nullptr;
    }
    // url + '/' + path
    std::string url_str = _url->url;
    std::string path_str = mstr::urlEncode(path);
    if (url_str.back() != '/') url_str.push_back('/');
    if (path_str.front() == '/') path_str.erase(0, 1);
    url_str += path_str;
    if (!_http->begin(url_str))
    {
        Debug_println("FileSystemHTTP::cache_file - failed to start HTTP client");
        return nullptr;
	}

    // GET request
    Debug_println("Initiating GET request");
    if (_http->GET() > 399)
    {
        Debug_println("FileSystemHTTP::cache_file - GET failed");
        return nullptr;
    }

    // Retrieve HTTP data
    int tmout_counter = 1 + HTTP_GET_TIMEOUT / 50;
    bool cancel = false;
    int available;

    // Allocate copy buffer
#ifdef ESP_PLATFORM
    uint8_t *buf = (uint8_t *)heap_caps_malloc(COPY_BLK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    uint8_t *buf = (uint8_t *)malloc(COPY_BLK_SIZE);
#endif
    if (buf == nullptr)
    {
        Debug_println("FileSystemHTTP::cache_file - failed to allocate buffer");
        return nullptr;
    }

    // Process all response chunks
    Debug_println("Retrieving file data");
    while ( !cancel )
    {
        available = _http->available();
        if (_http->is_transaction_done() && available == 0) // done
            break;

        if (available == 0) // http transaction not completed, wait for data
        {
            if (--tmout_counter == 0)
            {
                // no new data in time
                Debug_println("FileSystemHTTP::cache_file - Timeout");
                cancel = true;
                break;
            }
            fnSystem.delay(50); // wait
        }
        else if (available > 0)
        {
            Debug_printf("data available: %d\n", available);
            while (available > 0)
            {
                // Read HTTP data
                int to_read = (available > COPY_BLK_SIZE) ? COPY_BLK_SIZE : available;
                int from_read = _http->read(buf, to_read);
                if (from_read != to_read) // TODO: is it really an error?
                {
                    Debug_println("FileSystemHTTP::cache_file - HTTP read failed");
                    Debug_printf("  Expected %d bytes, actually got %d bytes.\r\n", to_read, from_read);
                    cancel = true;
                    break;
                }
                // Write cache file
                if (FileCache::write(fc, buf, to_read) < to_read)
                {
                    Debug_printf("FileSystemHTTP::cache_file - Cache write failed\n");
                    cancel = true;
                    break;
                }
                // Next chunk
                available = _http->available();
            }
            tmout_counter = 1 + HTTP_GET_TIMEOUT / 50; // reset timeout counter
        }
        else if (available < 0)
        {
            Debug_println("FileSystemHTTP::cache_file - something went wrong");
            cancel = true;
        }
    }
    // Release copy buffer
    free(buf);

    // Dispose HTTP client
    delete _http;
    _http = nullptr;

    if (cancel)
    {
        Debug_println("Cancelled");
        FileCache::remove(fc);
        fh = nullptr;
    }
    else
    {
        Debug_println("File data retrieved");
        fh = FileCache::reopen(fc, mode);
    }

    HEAP_DEBUG();
    return fh;
}
#endif //!FNIO_IS_STDIO

bool FileSystemHTTP::is_dir(const char *path)
{
    // TODO
    return false;
}

bool FileSystemHTTP::dir_open(const char  *path, const char *pattern, uint16_t diropts)
{
    if(!_started)
        return false;

    Debug_printf("FileSystemHTTP::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);
    HEAP_DEBUG();

    if (_http != nullptr)
    {
        delete _http;
        _http = nullptr;
    }

    if (path == nullptr)
        return false;

    if (strcmp(_last_dir, path) == 0 && !_dircache.empty())
    {
        Debug_printf("Use directory cache\n");
    }
    else
    {
        Debug_printf("Fill directory cache\n");

        _dircache.clear();
        // invalidate _last_dir
        _last_dir[0] = '\0';

        // Setup HTTP client
        _http = new HTTP_CLIENT_CLASS();
        if (_http == nullptr)
        {
            Debug_println("FileSystemHTTP::dir_open() - failed to create HTTP client\n");
            return false;
        }
        // url + '/' + path + '/'
        std::string url_str = _url->url;
        std::string path_str = mstr::urlEncode(path);
        if (url_str.back() != '/') url_str.push_back('/');
        if (path_str.front() == '/') path_str.erase(0, 1);
        url_str += path_str;
        if (url_str.back() != '/') url_str.push_back('/');
        if (!_http->begin(url_str))
        {
            Debug_println("FileSystemHTTP::dir_open - failed to start HTTP client");
            return false;
        }
    
        // Setup HTML Index parser
        if (_parser.begin_parser())
        {
            Debug_printf("Failed to setup parser.\r\n");
            return false;
        }

        // GET request
        Debug_println("Initiating GET request");
        if (_http->GET() > 399)
        {
            Debug_println("FileSystemHTTP::dir_open - GET failed");
            return false;
        }
    
        // Remember last visited directory
        strlcpy(_last_dir, path, MAX_PATHLEN);

        // Read & parse Index of directory
        int tmout_counter = 1 + HTTP_GET_TIMEOUT / 50;
        bool cancel = false;
        int available;

        // Allocate copy buffer
#ifdef ESP_PLATFORM
        uint8_t *buf = (uint8_t *)heap_caps_malloc(COPY_BLK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        uint8_t *buf = (uint8_t *)malloc(COPY_BLK_SIZE);
#endif
        if (buf == nullptr)
        {
            Debug_println("FileSystemHTTP::dir_open - failed to allocate buffer");
            return false;
        }

        // Process all response chunks
        Debug_println("Retrieving directory data");
        while ( !cancel )
        {
            available = _http->available();
            if (_http->is_transaction_done() && available == 0) // done
                break;

            if (available == 0) // http transaction not completed, wait for data
            {
                if (--tmout_counter == 0)
                {
                    // no new data in time
                    Debug_println("FileSystemHTTP::dir_open - Timeout");
                    cancel = true;
                    break;
                }
                fnSystem.delay(50); // wait
            }
            else if (available > 0)
            {
                Debug_printf("data available: %d\n", available);
                while (available > 0)
                {
                    // read HTTP data
                    int to_read = available > COPY_BLK_SIZE ? COPY_BLK_SIZE : available;
                    int from_read = _http->read(buf, to_read);
                    if (from_read != to_read) // TODO: is it really an error?
                    {
                        Debug_println("FileSystemHTTP::dir_open - HTTP read failed");
                        Debug_printf("  Expected %d bytes, actually got %d bytes.\r\n", to_read, from_read);
                        cancel = true;
                        break;
                    }
    
                    // Parse the buffer
                    if (_parser.parse((char *)buf, from_read, false))
                    {
                        Debug_printf("Could not parse buffer\r\n");
                        cancel = true;
                        break;
                    }

                    // next chunk
                    available = _http->available();
                }
                tmout_counter = 1 + HTTP_GET_TIMEOUT / 50; // reset timeout counter
            }
            else if (available < 0)
            {
                Debug_println("FileSystemHTTP::cache_file - something went wrong");
                cancel = true;
            }
        }
        // release copy buffer
        free(buf);

        // Dispose http client
        delete _http;
        _http = nullptr;

        if (cancel)
        {
            Debug_println("Cancelled");
            _parser.clear();
            return false;
        }
        else
        {
            Debug_println("Directory data retrieved");
        }


        // finish parsing (not sure if this is necessary)
        _parser.parse(nullptr, 0, true);

        // Release parser resources (but keep parsed directory entries)
        _parser.end_parser();

        // Parsed entries to dircache
        fsdir_entry *fs_de;
#ifdef ESP_PLATFORM
        std::vector<IndexParser::IndexEntry,PSRAMAllocator<IndexParser::IndexEntry>>::iterator dirEntryCursor = _parser.rewind();
#else
        std::vector<IndexParser::IndexEntry>::iterator dirEntryCursor = _parser.rewind();
#endif
        struct tm tm;
        while (dirEntryCursor != _parser.entries.end())
        {
            // new dir entry
            fs_de = &_dircache.new_entry();

            // Set entry members

            // file name
            strlcpy(fs_de->filename, mstr::urlDecode(dirEntryCursor->filename, false).c_str(), sizeof(fs_de->filename));
            fs_de->isDir = dirEntryCursor->isDir;

            // file size
            std::string fileSize = dirEntryCursor->fileSize;
            fileSize.erase(fileSize.find_last_not_of(" \t") + 1); // trim trailing spaces
            mstr::toUpper(fileSize);
            double sizeValue = std::atof(fileSize.c_str());
            size_t pos = fileSize.find_last_of("KMGTP"); // fs_de->size is uint32_t, up to 4GB
            if (pos == std::string::npos)
            {
                fs_de->size = static_cast<uint32_t>(sizeValue);
            }
            else
            {
                // convert size with suffix to bytes
                switch (fileSize[pos])
                {
                case 'K':
                    fs_de->size = static_cast<uint32_t>(sizeValue * 1024);
                    break;
                case 'M':
                    fs_de->size = static_cast<uint32_t>(sizeValue * 1024 * 1024);
                    break;
                case 'G':
                    fs_de->size = static_cast<uint32_t>(sizeValue * 1024 * 1024 * 1024);
                    break;
                case 'T':
                case 'P':
                    fs_de->size = ~1; // set to max, regardless of value
                    break;
                }
            }
        
            //file modification time
            fs_de->modified_time = 0;
            memset(&tm, 0, sizeof(struct tm));
            // strptime is not available on Windows ... 
            // if (strptime(dirEntryCursor->mTime.c_str(), "%d-%b-%Y %H:%M", &tm) != nullptr)
            // use std::get_time instead
            std::istringstream ss(dirEntryCursor->mTime);
            ss >> std::get_time(&tm, "%d-%b-%Y %H:%M");
            if (!ss.fail()) 
            {
                tm.tm_isdst = -1;
                fs_de->modified_time = mktime(&tm);
            }
            else
            {
                // Rewind the stringstream to the beginning
                ss.clear(); // Clear any error flags
                ss.seekg(0, std::ios::beg); // Rewind to the beginning
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
                if (!ss.fail()) 
                {
                    tm.tm_isdst = -1;
                    fs_de->modified_time = mktime(&tm);
                }
            }

            if (fs_de->isDir)
            {
                Debug_printf(" add entry: \"%s\"\tDIR\n", fs_de->filename);
            }
            else
            {
                Debug_printf(" add entry: \"%s\"\t%lu\n", fs_de->filename, fs_de->size);
            }

            dirEntryCursor++;
        }
        // Delete parsed entries
        _parser.clear();
    }

    // Apply pattern matching filter and sort entries
    _dircache.apply_filter(pattern, diropts);

    HEAP_DEBUG();
    return true;
}

fsdir_entry *FileSystemHTTP::dir_read()
{
    return _dircache.read();
}

void FileSystemHTTP::dir_close()
{
    // _dircache.clear();
}

uint16_t FileSystemHTTP::dir_tell()
{
    return _dircache.tell();
}

bool FileSystemHTTP::dir_seek(uint16_t pos)
{
    return _dircache.seek(pos);
}
