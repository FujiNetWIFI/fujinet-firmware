
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

    // Store credentials for reconnection
    _username = (user == nullptr ? "anonymous" : user);
    _password = (password == nullptr ? "fujinet@fujinet.online" : password);

    res = _ftp->login(
        _username.c_str(),
        _password.c_str(),
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
    if (!ensure_connected() || path == nullptr)
        return false;
    // TODO
    Debug_printf("FileSystemFTP::exists(\"%s\")\n", path);

    // Use LIST to check if path exists (works for both files and directories)
    protocolError_t res = _ftp->open_directory(path, "");
    
    if (res != PROTOCOL_ERROR::NONE)  // open_directory returns PROTOCOL_ERROR::NONE on success
    {
        Debug_printf("Path does not exist\n");
        return false;
    }

    // Read at least one entry to confirm it exists
    string filename;
    long filesz;
    bool is_directory;
    
    res = _ftp->read_directory(filename, filesz, is_directory);
    bool exists = (res == PROTOCOL_ERROR::NONE && !filename.empty());
    
    Debug_printf("Path %s\n", exists ? "exists" : "does not exist");
    return exists;
}

bool FileSystemFTP::remove(const char *path)
{
    if (!_started || path == nullptr)
        return false;

    Debug_printf("FileSystemFTP::remove(\"%s\")\n", path);

    // Attempt to delete the file
    // delete_file returns FALSE on success, TRUE on error
    if (_ftp->delete_file(path) == PROTOCOL_ERROR::NONE)
    {
        Debug_printf("File deleted successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to delete file\n");
        return false;
    }
}

bool FileSystemFTP::rename(const char *pathFrom, const char *pathTo)
{
    if (!_started || pathFrom == nullptr || pathTo == nullptr)
        return false;

    Debug_printf("FileSystemFTP::rename(\"%s\" -> \"%s\")\n", pathFrom, pathTo);

    // Attempt to rename the file
    // rename_file returns FALSE on success, TRUE on error
    if (_ftp->rename_file(pathFrom, pathTo) == PROTOCOL_ERROR::NONE)
    {
        Debug_printf("File renamed successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to rename file\n");
        return false;
    }
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
    Debug_printf("FileSystemFTP::cache_file(\"%s\", \"%s\")\n", path, mode);

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
            break;
        }
        else if (available > 0)
        {
            while (available > 0)
            {
                if (_ftp->data_connected() == PROTOCOL_ERROR::NONE) // done
                    break;

                // Read FTP data
                int to_read = available > COPY_BLK_SIZE ? COPY_BLK_SIZE : available;
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
    if (!ensure_connected() || path == nullptr)
        return false;

    Debug_printf("FileSystemFTP::is_dir(\"%s\")\n", path);

    // Try to open the path as a directory
    protocolError_t res = _ftp->open_directory(path, "");
    
    if (res != PROTOCOL_ERROR::NONE)  // open_directory returns PROTOCOL_ERROR::NONE on success
    {
        Debug_printf("Failed to LIST path\n");
        return false;
    }

    // Read the first entry to check if it's a directory
    // TODO
    // For a directory, LIST returns its contents
    string filename;
    long filesz;
    bool is_directory;
    
    res = _ftp->read_directory(filename, filesz, is_directory);
    if (res == PROTOCOL_ERROR::NONE && !filename.empty())
    {
        // Successfully read an entry
        // If the filename matches the path (or just the basename), it's listing the file itself
        // Some servers return full path, others return just basename
        
        if (strcmp(path, filename.c_str()) == 0)
        {
            // Full path matches - it's listing the file itself
            Debug_printf("Path is %s (is_dir=%d)\n", is_directory ? "a directory" : "a file", is_directory);
            return is_directory;
        }
        
        const char *last_slash = strrchr(path, '/');
        const char *basename = last_slash ? last_slash + 1 : path;
        
        const char *file_last_slash = strrchr(filename.c_str(), '/');
        const char *file_basename = file_last_slash ? file_last_slash + 1 : filename.c_str();
        
        if (strcmp(basename, file_basename) == 0)
        {
            // Basename matches - it's listing the file itself
            Debug_printf("Path is %s (is_dir=%d)\n", is_directory ? "a directory" : "a file", is_directory);
            return is_directory;
        }
        else
        {
            // Different name - it's listing directory contents
            Debug_printf("Path is a directory (listing contents)\n");
            return true;
        }
    }
    
    Debug_printf("Could not determine if path is directory\n");
    return false;
}

bool FileSystemFTP::mkdir(const char* path)
{
    if (!_started || path == nullptr)
        return false;

    Debug_printf("FileSystemFTP::mkdir(\"%s\")\n", path);

    // Attempt to create the directory
    // make_directory returns FALSE on success, TRUE on error
    if (_ftp->make_directory(path) == PROTOCOL_ERROR::NONE)
    {
        Debug_printf("Directory created successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to create directory\n");
        return false;
    }
}

bool FileSystemFTP::rmdir(const char* path)
{
    if (!_started || path == nullptr)
        return false;

    Debug_printf("FileSystemFTP::rmdir(\"%s\")\n", path);

    // Attempt to remove the directory
    // remove_directory returns FALSE on success, TRUE on error
    if (_ftp->remove_directory(path) == PROTOCOL_ERROR::NONE)
    {
        Debug_printf("Directory removed successfully\n");
        return true;
    }
    else
    {
        Debug_printf("Failed to remove directory\n");
        return false;
    }
}

bool FileSystemFTP::dir_exists(const char* path)
{
    // dir_exists is essentially the same as is_dir for FTP
    return is_dir(path);
}

bool FileSystemFTP::dir_open(const char  *path, const char *pattern, uint16_t diropts)
{
    if (!ensure_connected())
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

bool FileSystemFTP::keep_alive()
{
    if (!_started)
        return false;

    // Send NOOP command as lightweight keep-alive
    protocolError_t res = _ftp->keep_alive();
    
    if (res != PROTOCOL_ERROR::NONE) {
        Debug_printf("FTP keep_alive failed - marking session as disconnected\n");
        _started = false;
    }
    
    return res == PROTOCOL_ERROR::NONE;
}

bool FileSystemFTP::ensure_connected()
{
    // Check if we're actually connected at the FTP protocol level
    if (_started && _ftp && _ftp->control_connected()) {
        return true;  // Already connected and verified
    }
    
    // If we thought we were connected but aren't, mark as disconnected
    if (_started) {
        Debug_printf("FTP control connection lost, attempting reconnect\n");
        _started = false;
    }
    
    if (!_url || !_ftp) {
        Debug_printf("Cannot connect - missing URL or FTP client\n");
        return false;
    }
    
    if (_username.empty()) {
        Debug_printf("Cannot connect - credentials not set (start() was never called)\n");
        return false;
    }
    
    Debug_printf("Attempting to connect to FTP server: %s\n", _url->host.c_str());
    
    // Attempt to connect using stored credentials
    protocolError_t res = _ftp->login(
        _username.c_str(),
        _password.c_str(),
        _url->host,
        _url->port.empty() ? 21 : atoi(_url->port.c_str())
    );
    
    if (res != PROTOCOL_ERROR::NONE) {
        Debug_printf("Failed to connect to FTP server\n");
        return false;
    }
    
    Debug_printf("Successfully connected to FTP server\n");
    _started = true;
    return true;
}