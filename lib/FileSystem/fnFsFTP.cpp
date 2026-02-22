
#include "fnFsFTP.h"

#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFileCache.h"

#define COPY_BLK_SIZE 4096

FileSystemFTP::FileSystemFTP()
{
    Debug_printf("FileSystemFTP::ctor\n");
    _ftp = nullptr;
    _url = nullptr;
    // invalidate _last_dir
    _last_dir[0] = '\0';
}

FileSystemFTP::~FileSystemFTP()
{
    Debug_printf("FileSystemFTP::dtor\n");
    if (_started)
    {
        _dircache.clear();
        _ftp->logout();
    }
    if (_ftp != nullptr)
        delete _ftp;
}

bool FileSystemFTP::start(const char *url, const char *user, const char *password)
{
    protocolError_t res;

    if (_started)
        return false;

    if(url == nullptr || url[0] == '\0')
        return false;

    if (_ftp != nullptr)
        delete _ftp;

    _ftp = new fnFTP();
    if (_ftp == nullptr)
    {
        Debug_printf("FileSystemFTP::start() - failed to create FTP client\n");
        return false;
    }

    _url = PeoplesUrlParser::parseURL(url);
    if (!_url->isValidUrl())
    {
        Debug_printf("FileSystemFTP::start() - failed to parse URL \"%s\"\n", url);
        return false;
    }

    res = _ftp->login(
        user == nullptr ? "anonymous" : user,
        password == nullptr ? "fujinet@fujinet.online" : password,
        _url->host,
        _url->port.empty() ? 21 : atoi(_url->port.c_str())
    );

    if (res != PROTOCOL_ERROR::NONE)
    {
        Debug_printf("FileSystemFTP::start() - FTP login failed: %s\n", _url->host.c_str());
        return false;
        }

    Debug_printf("FTP logged in: %s\n", _url->host.c_str());

    _started = true;

    return true;
}

bool FileSystemFTP::exists(const char *path)
{
    // TODO
    return false;
}

bool FileSystemFTP::remove(const char *path)
{
    return false;
}

bool FileSystemFTP::rename(const char *pathFrom, const char *pathTo)
{
    return false;
}

FILE  *FileSystemFTP::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemFTP::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemFTP::filehandler_open(const char *path, const char *mode)
{
    FileHandler *fh = cache_file(path, mode);
    return fh;
}

// Read file from FTP path and write it to cache file
// Return FileHandler* on success (memory or SD file), nullptr on error
FileHandler *FileSystemFTP::cache_file(const char *path, const char *mode)
{
    // Try SD cache first
    FileHandler *fh = FileCache::open(_url->mRawUrl.c_str(), path, mode);
    if (fh != nullptr)
        return fh; // cache hit, done

    // Create new cache file (starts in memory)
    fc_handle *fc = FileCache::create(_url->mRawUrl.c_str(), path);
    if (fc == nullptr)
        return nullptr;

    // Open FTP file
    Debug_println("Initiating file RETR");
    if (_ftp->open_file(path, false) != PROTOCOL_ERROR::NONE)
    {
        Debug_println("FileSystemFTP::cache_file - RETR failed");
        return nullptr;
    }

    // Retrieve FTP data
    int tmout_counter = 1 + FTP_TIMEOUT / 50;
    bool cancel = false;
    int available;

    // Allocate copy buffer
    // uint8_t *buf = (uint8_t *)heap_caps_malloc(COPY_BLK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buf = (uint8_t *)malloc(COPY_BLK_SIZE);
    if (buf == nullptr)
    {
        Debug_println("FileSystemFTP::cache_file - failed to allocate buffer");
        return nullptr;
    }

    Debug_println("Retrieving file data");
    while ( !cancel )
    {
        available = _ftp->data_available();
        if (_ftp->data_connected() == PROTOCOL_ERROR::NONE) // done
            break;

        if (available == 0)
        {
            if (--tmout_counter == 0)
            {
                // no data & no control message
                Debug_println("FileSystemFTP::cache_file - Timeout");
                cancel = true;
                break;
            }
            fnSystem.delay(50); // wait for more data or control message
        }
        else if (available > 0)
        {
            Debug_printf("data available: %d\n", available);
            while (available > 0)
            {
                // Read FTP data
                int to_read = available > sizeof(buf) ? sizeof(buf) : available;
                if (_ftp->read_file(buf, to_read) != PROTOCOL_ERROR::NONE)
                {
                    Debug_println("FileSystemFTP::cache_file - FTP read failed");
                    cancel = true;
                    break;
                }
                // Write cache file
                if (FileCache::write(fc, buf, to_read) < to_read)
                {
                    Debug_printf("FileSystemFTP::cache_file - Cache write failed\n");
                    cancel = true;
                    break;
                }
                // Next chunk
                available = _ftp->data_available();
            }
            tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
        }
        else if (available < 0)
        {
            Debug_println("FileSystemFTP::cache_file - something went wrong");
            cancel = true;
        }
    }
    // Release copy buffer
    free(buf);

    // Close FTP client
    _ftp->close();

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
    return fh;
}
#endif //!FNIO_IS_STDIO

bool FileSystemFTP::is_dir(const char *path)
{
    // TODO
    return false;
}

bool FileSystemFTP::dir_open(const char  *path, const char *pattern, uint16_t diropts)
{
    if(!_started)
        return false;

    Debug_printf("FileSystemFTP::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);

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

        // List FTP directory
        protocolError_t res;
        res = _ftp->open_directory(path, "");

        if (res != PROTOCOL_ERROR::NONE)
        {
            Debug_printf("Failed to open directory\n");
            return false;
        }

        // Remember last visited directory
        strlcpy(_last_dir, path, MAX_PATHLEN);

        // Populate directory cache with entries
        string filename;
        long filesz;
        bool is_dir;
        fsdir_entry *fs_de;

        // get first directory entry
        res = _ftp->read_directory(filename, filesz, is_dir);
        while(res == PROTOCOL_ERROR::NONE)
        {
            // skip hidden
            if (filename[0] == '.')
                continue;

            // new dir entry
            fs_de = &_dircache.new_entry();

            // set entry members
            strlcpy(fs_de->filename, filename.c_str(), sizeof(fs_de->filename));
            fs_de->isDir = is_dir;
            fs_de->size = (uint32_t)filesz;
            fs_de->modified_time = 0; // TODO

            // get next
            res = _ftp->read_directory(filename, filesz, is_dir);
        }
    }

    // Apply pattern matching filter and sort entries
    _dircache.apply_filter(pattern, diropts);

    return true;
}

fsdir_entry *FileSystemFTP::dir_read()
{
    return _dircache.read();
}

void FileSystemFTP::dir_close()
{
    // _dircache.clear();
}

uint16_t FileSystemFTP::dir_tell()
{
    return _dircache.tell();
}

bool FileSystemFTP::dir_seek(uint16_t pos)
{
    return _dircache.seek(pos);
}
