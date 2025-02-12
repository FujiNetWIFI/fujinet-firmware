
#include "fnFsHTTP.h"

#include <ctime>

#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFileMem.h"
#include "fnFsSD.h"
#include "string_utils.h"

#define MAX_CACHE_MEMFILE_SIZE  204800

// http timeout in ms
#define HTTP_GET_TIMEOUT 7000

// TODO: create global utility function
#include "mbedtls/md5.h"
static void get_md5_string(const unsigned char *buf, size_t size, char *result)
{
    unsigned char md5_result[16];
    mbedtls_md5(buf, size, md5_result);
    for (int i=0; i < 16; i++)
    {
        sprintf(&result[i * 2], "%02x",  md5_result[i]);
    }
}

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
        delete _http;

    _http = new HTTP_CLIENT_CLASS();
    if (_http == nullptr)
    {
        Debug_println("FileSystemHTTP::start() - failed to create HTTP client\n");
        return false;
    }

    _url = PeoplesUrlParser::parseURL(url);
    if (!_url->isValidUrl())
    {
        Debug_printf("FileSystemHTTP::cache_file() - failed to parse URL \"%s\"\n", _url->url.c_str());
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
    FileHandler *fh = cache_file(path);
    return fh;
}

// read file from HTTP path and write it to cache file
// return FileHandler* on success (memory or SD file), nullptr on error
FileHandler *FileSystemHTTP::cache_file(const char *path)
{
	if (!_http->begin(_url->url + mstr::urlEncode(path)))
    {
        Debug_println("FileSystemHTTP::cache_file() - failed to start HTTP client");
        return nullptr;
	}

    // open cache memory file
    FileHandler *fh = new FileHandlerMem;
    if (fh == nullptr)
    {
        Debug_println("FileSystemHTTP::cache_file - failed to open memory file");
        return nullptr;
    }

    // GET request
    Debug_println("Initiating GET request");
    if (_http->GET() > 399)
    {
        Debug_println("FileSystemHTTP::cache_file - GET failed");
        return nullptr;
    }

    // copy HTTP to file
    uint8_t buf[1024];
    int tmout_counter = 1 + HTTP_GET_TIMEOUT / 50;
    size_t bytes_read = 0;
    bool use_memfile = true;
    bool cancel = false;

    // Process all response chunks
    Debug_println("Reading HTTP data");
    while ( !_http->is_transaction_done() || _http->available() > 0)
    {
        int available = _http->available();
        if (available == 0)
        {
            if (--tmout_counter == 0)
            {
                // no new data
                Debug_println("FileSystemHTTP::cache_file - Timeout");
                cancel = true;
                break;
            }
            fnSystem.delay(50); // wait for more data
            available = _http->available();
        }

        if (available > 0)
        {
            Debug_printf("data available %d ...\n", available);
            while (available > 0)
            {
                // read HTTP data
                int to_read = available > sizeof(buf) ? sizeof(buf) : available;
                int from_read = _http->read(buf, to_read);
                if (from_read != to_read) // TODO: is it really an error?
                {
                    Debug_printf("Expected %d bytes, actually got %d bytes.\r\n", to_read, from_read);
                    cancel = true;
                    break;
                }
                // write cache file
                if (fh->write(buf, 1, to_read) < to_read)
                {
                    Debug_printf("FileSystemHTTP::cache_file - write failed\n");
                    cancel = true;
                    break;
                }
                bytes_read += to_read;

                // check if memory file is over limit
                if (use_memfile && bytes_read > MAX_CACHE_MEMFILE_SIZE)
                {
                    // for large files switch from memory to SD card
                    if (!fnSDFAT.running())
                    {
                        Debug_println("FileSystemHTTP::cache_file - SD Filesystem is not running");
                        cancel = true;
                        break;
                    }

                    // SD file path, use MD5 of host url and MD5 of file path
                    char cache_path[] = "/FujiNet/cache/........-................................";
                    get_md5_string((const unsigned char *)(_url->mRawUrl.c_str()), _url->mRawUrl.length(), cache_path + 15);
                    get_md5_string((const unsigned char *)path, strlen(path), cache_path + 15 + 9);
                    cache_path[15 + 8] = '-';
                    Debug_printf("Using SD cache file: %s\n", cache_path);

                    // ensure cache directory exists
                    fnSDFAT.create_path("/FujiNet/cache");

                    // open SD file
                    FileHandler *fh_sd = fnSDFAT.filehandler_open(cache_path, "wb+");
                    if (fh_sd == nullptr)
                    {
                        Debug_println("FileSystemHTTP::cache_file - failed to open SD file");
                        cancel = true;
                        break;
                    }

                    // copy from memory to SD file
                    size_t count = 0;
                    fh->seek(0, SEEK_SET);
                    do
                    {
                        count = fh->read(buf, 1, sizeof(buf));
                        fh_sd->write(buf, 1, count);
                    } while (count > 0);
                    fh->close();
                    // switch to file on SD
                    fh = fh_sd;
                    use_memfile = false;
                }

                // next batch
                available = _http->available();
            }
            tmout_counter = 1 + HTTP_GET_TIMEOUT / 50; // reset timeout counter
        }
    }
    _http->close();

    if (cancel)
    {
        fh->close();
        fh = nullptr;
    }
    else
    {
        fh->seek(0, SEEK_SET);
    }
    return fh;
}
#endif

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

    // TODO: Why http client needs to be re-created? Would be better to re-use it.
#ifdef ESP_PLATFORM
    if (_http != nullptr)
        delete _http;

    _http = new HTTP_CLIENT_CLASS();
    if (_http == nullptr)
    {
        Debug_println("FileSystemHTTP::start() - failed to create HTTP client\n");
        return false;
    }
#endif

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

        if (!_http->begin(_url->url + mstr::urlEncode(path)))
        {
            Debug_println("FileSystemHTTP::dir_open - failed to start HTTP client");
            return false;
        }
    
        // Setup XML IndexParser parser
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

        // read & parse Index of directory
        uint8_t buf[1024];
        int tmout_counter = 1 + HTTP_GET_TIMEOUT / 50;
        size_t bytes_read = 0;
        bool cancel = false;
    
        // Process all response chunks
        Debug_println("Reading HTTP data");
        while ( !_http->is_transaction_done() || _http->available() > 0)
        {
            int available = _http->available();
            if (available == 0)
            {
                if (--tmout_counter == 0)
                {
                    // no new data
                    Debug_println("FileSystemHTTP::dir_open - Timeout");
                    cancel = true;
                    break;
                }
                fnSystem.delay(50); // wait for more data
                available = _http->available();
            }
    
            if (available > 0)
            {
                Debug_printf("data available %d ...\n", available);
                while (available > 0)
                {
                    // read HTTP data
                    int to_read = available > sizeof(buf) ? sizeof(buf) : available;
                    int from_read = _http->read(buf, to_read);
                    if (from_read != to_read) // TODO: is it really an error?
                    {
                        Debug_printf("Expected %d bytes, actually got %d bytes.\r\n", to_read, from_read);
                        cancel = true;
                        break;
                    }
                    bytes_read += to_read;
    
                    // Parse the buffer
                    if (_parser.parse((char *)buf, from_read, false))
                    {
                        Debug_printf("Could not parse buffer\r\n");
                        cancel = true;
                        break;
                    }

                    // next batch
                    available = _http->available();
                }
                tmout_counter = 1 + HTTP_GET_TIMEOUT / 50; // reset timeout counter
            }
        }
        _http->close();

        if (cancel)
        {
            _parser.clear();
            return false;
        }

        // finish parsing (not sure if this is necessary)
        _parser.parse(nullptr, 0, true);

        // Release parser resources (but keep parsed directory entries)
        _parser.end_parser();

        // Parsed entries to dircache
        fsdir_entry *fs_de;
        std::vector<IndexParser::IndexEntry>::iterator dirEntryCursor = _parser.rewind();
        while (dirEntryCursor != _parser.entries.end())
        {
            // new dir entry
            fs_de = &_dircache.new_entry();

            // set entry members
            strlcpy(fs_de->filename, mstr::urlDecode(dirEntryCursor->filename).c_str(), sizeof(fs_de->filename));
            fs_de->isDir = dirEntryCursor->isDir;
            fs_de->size = (uint32_t)atoi(dirEntryCursor->fileSize.c_str());
            // attempt to get file modification time
            fs_de->modified_time = 0;
            struct tm tm;
            memset(&tm, 0, sizeof(struct tm));
            if (strptime(dirEntryCursor->mTime.c_str(), "%d-%b-%Y %H:%M", &tm) != nullptr)
            {
                tm.tm_isdst = -1;
                fs_de->modified_time = mktime(&tm);
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
