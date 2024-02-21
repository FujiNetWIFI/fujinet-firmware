
#include "fnFsFTP.h"

#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFileMem.h"
#include "fnFsSD.h"

#define MAX_CACHE_MEMFILE_SIZE  204800

#include "mbedtls/md5.h"
void get_md5_string(const unsigned char *buf, size_t size, char *result)
{
    unsigned char md5_result[16];
    mbedtls_md5(buf, size, md5_result);
    for (int i=0; i < 16; i++)
    {
        sprintf(&result[i * 2], "%02x",  md5_result[i]);
    }
}

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
    bool res;

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

	if (res)
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
    FileHandler *fh = cache_file(path);
    return fh;
}

// read file from FTP path and write it to cache file
// return FileHandler* on success (memory or SD file), nullptr on error
FileHandler *FileSystemFTP::cache_file(const char *path)
{
    // open FTP file
    if (_ftp->open_file(path, false))
    {
        Debug_printf("FileSystemFTP::cache_file - Failed to open file\n");
        return nullptr;
    }

    // open cache memory file
    FileHandler *fh = new FileHandlerMem;
    if (fh == nullptr)
    {
        Debug_println("FileSystemFTP::cache_file - failed to open memory file");
        return nullptr;
    }

    // copy FTP to file

    uint8_t buf[1024];
    int tmout_counter = 1 + FTP_TIMEOUT / 50;
    size_t bytes_read = 0;
    bool use_memfile = true;
    bool cancel = false;

    do
    {
        int available = _ftp->data_available();
        if (available == 0)
        {
            if (--tmout_counter == 0)
            {
                // no data & no control message
                Debug_printf("FileSystemFTP::cache_file - Timeout\n");
                cancel = true;
                break;
            }
            fnSystem.delay(50); // wait for more data or control message
            available = _ftp->data_available();
        }
        
        if (available > 0)
        {
            Debug_printf("Reading FTP data\n");
            while (available > 0)
            {
                // read FTP data
                int to_read = available > sizeof(buf) ? sizeof(buf) : available;
                if (_ftp->read_file(buf, to_read))
                {
                    Debug_printf("FileSystemFTP::cache_file - read failed\n");
                    cancel = true;
                    break;
                }
                // write cache file
                if (fh->write(buf, 1, to_read) < to_read)
                {
                    Debug_printf("FileSystemFTP::cache_file - write failed\n");
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
                        Debug_println("FileSystemFTP::cache_file - SD Filesystem is not running");
                        cancel = true;
                        break;
                    }

                    // SD file path, use MD5 of host url and MD5 of file path
                    char cache_path[] = "/FujiNet/cache/........-................................";
                    get_md5_string((const unsigned char *)(_url->mRawUrl.c_str()), _url->mRawUrl.length(), cache_path + 15);
                    get_md5_string((const unsigned char *)path, strlen(path), cache_path + 15 + 9);
                    cache_path[15 + 8] = '-';
                    Debug_printf("SD cache file: %s\n", cache_path);

                    // ensure cache directory exists
                    fnSDFAT.create_path("/FujiNet/cache");

                    // open SD file
                    FileHandler *fh_sd = fnSDFAT.filehandler_open(cache_path, "wb+");
                    if (fh_sd == nullptr)
                    {
                        Debug_println("FileSystemFTP::cache_file - failed to open SD file");
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
                available = _ftp->data_available();
            }
            tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
        }
    } while (!cancel && _ftp->data_connected());
    _ftp->close();

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
        bool res;
        res = _ftp->open_directory(path, "");

        if (res)
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
        while(res == false)
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
