
#include "fnFsGDrive.h"

#include <cstring>

#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFileCache.h"

// http timeout in ms while streaming a download
#define GDRIVE_GET_TIMEOUT 20000

#define COPY_BLK_SIZE 4096

FileSystemGDrive::FileSystemGDrive()
{
    Debug_printf("FileSystemGDrive::ctor\n");
    _last_dir[0] = '\0';
}

FileSystemGDrive::~FileSystemGDrive()
{
    Debug_printf("FileSystemGDrive::dtor\n");
    if (_started)
        _dircache.clear();
}

success_is_true FileSystemGDrive::start(const char *url, const char *user, const char *password)
{
    if (_started)
        RETURN_ERROR_AS_FALSE();

    if (url == nullptr || url[0] == '\0')
        RETURN_ERROR_AS_FALSE();

    // The host string is just the GDRIVE:// scheme (Drive itself is the host);
    // keep it verbatim as the file-cache key so cached images are namespaced
    // per host slot.
    _rawurl = url;

    // We need a usable OAuth2 access token to talk to Drive. If the user has
    // not authorized (no refresh token), fail the mount so the slot shows as
    // disconnected rather than silently empty.
    if (!_gdrive.ensure_access_token())
    {
        Debug_printf("FileSystemGDrive::start() - no Google Drive access token (authorize in web UI first)\n");
        RETURN_ERROR_AS_FALSE();
    }

    Debug_println("FileSystemGDrive started");

    RETURN_SUCCESS_IF(_started = true);
}

bool FileSystemGDrive::exists(const char *path)
{
    if (!_started || path == nullptr)
        return false;

    _gdrive.ensure_access_token();
    bool found = !_gdrive.resolve_path(path).empty();
    Debug_printf("FileSystemGDrive::exists(\"%s\") = %d\n", path, found);
    return found;
}

success_is_true FileSystemGDrive::remove(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    _gdrive.ensure_access_token();
    std::string id = _gdrive.resolve_path(path);
    if (id.empty())
        RETURN_ERROR_AS_FALSE();

    // Removing changes the listing — invalidate the cached directory.
    _last_dir[0] = '\0';

    RETURN_SUCCESS_IF(_gdrive.api_delete(std::string(GDRIVE_FILES_URL) + "/" + id));
}

success_is_true FileSystemGDrive::rename(const char *pathFrom, const char *pathTo)
{
    // Google Drive rename requires a PATCH on the file metadata, which is not
    // implemented here (mirrors NetworkProtocolGDRIVE::rename_implemented=false).
    Debug_printf("FileSystemGDrive::rename() - not supported\n");
    RETURN_ERROR_AS_FALSE();
}

FILE *FileSystemGDrive::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemGDrive::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemGDrive::filehandler_open(const char *path, const char *mode)
{
    return cache_file(path, mode);
}

// Download the Drive file at `path` into the local file cache and return a
// FileHandler for it (memory or SD file). Returns nullptr on error.
FileHandler *FileSystemGDrive::cache_file(const char *path, const char *mode)
{
    Debug_printf("FileSystemGDrive::cache_file(\"%s\", \"%s\")\n", path, mode);

    // Try SD cache first
    FileHandler *fh = FileCache::open(_rawurl.c_str(), path, mode);
    if (fh != nullptr)
        return fh; // cache hit, done

    if (!_gdrive.ensure_access_token())
    {
        Debug_println("FileSystemGDrive::cache_file - no access token");
        return nullptr;
    }

    // Resolve the Drive file id for this path
    std::string file_id = _gdrive.resolve_path(path);
    if (file_id.empty())
    {
        Debug_printf("FileSystemGDrive::cache_file - not found: %s\n", path);
        return nullptr;
    }

    // Create new cache file (starts in memory)
    fc_handle *fc = FileCache::create(_rawurl.c_str(), path);
    if (fc == nullptr)
        return nullptr;

    // Stream the file content (?alt=media) into the cache file
    GDFS_HTTP_CLIENT http;
    std::string dl_url = std::string(GDRIVE_FILES_URL) + "/" + file_id + "?alt=media";
    if (!http.begin(dl_url))
    {
        Debug_println("FileSystemGDrive::cache_file - failed to start HTTP client");
        FileCache::remove(fc);
        return nullptr;
    }
    http.set_header("Authorization", ("Bearer " + _gdrive.access_token()).c_str());

    Debug_println("Initiating GET request");
    if (http.GET() > 399)
    {
        Debug_println("FileSystemGDrive::cache_file - GET failed");
        FileCache::remove(fc);
        return nullptr;
    }

    // Retrieve data
    int tmout_counter = 1 + GDRIVE_GET_TIMEOUT / 50;
    bool cancel = false;
    int available;

#ifdef ESP_PLATFORM
    uint8_t *buf = (uint8_t *)heap_caps_malloc(COPY_BLK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    uint8_t *buf = (uint8_t *)malloc(COPY_BLK_SIZE);
#endif
    if (buf == nullptr)
    {
        Debug_println("FileSystemGDrive::cache_file - failed to allocate buffer");
        FileCache::remove(fc);
        return nullptr;
    }

    Debug_println("Retrieving file data");
    while (!cancel)
    {
        available = http.available();
        if (http.is_transaction_done() && available == 0) // done
            break;

        if (available == 0) // transaction not completed, wait for data
        {
            if (--tmout_counter == 0)
            {
                Debug_println("FileSystemGDrive::cache_file - Timeout");
                cancel = true;
                break;
            }
            fnSystem.delay(50);
        }
        else if (available > 0)
        {
            while (available > 0)
            {
                int to_read = (available > COPY_BLK_SIZE) ? COPY_BLK_SIZE : available;
                int from_read = http.read(buf, to_read);
                if (from_read != to_read)
                {
                    Debug_println("FileSystemGDrive::cache_file - HTTP read failed");
                    Debug_printf("  Expected %d bytes, actually got %d bytes.\r\n", to_read, from_read);
                    cancel = true;
                    break;
                }
                if (FileCache::write(fc, buf, to_read) < (size_t)to_read)
                {
                    Debug_printf("FileSystemGDrive::cache_file - Cache write failed\n");
                    cancel = true;
                    break;
                }
                available = http.available();
            }
            tmout_counter = 1 + GDRIVE_GET_TIMEOUT / 50; // reset timeout counter
        }
        else // available < 0
        {
            Debug_println("FileSystemGDrive::cache_file - something went wrong");
            cancel = true;
        }
    }
    free(buf);
    http.close();

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

bool FileSystemGDrive::is_dir(const char *path)
{
    if (!_started || path == nullptr)
        return false;

    _gdrive.ensure_access_token();

    std::string id = _gdrive.resolve_path(path);
    if (id.empty())
        return false;
    if (id == "root")
        return true;

    std::string resp = _gdrive.api_get(std::string(GDRIVE_FILES_URL) + "/" + id + "?fields=mimeType");
    if (resp.empty())
        return false;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j)
        return false;
    bool is_folder = (GoogleDriveClient::json_str(j, "mimeType") == GDRIVE_FOLDER_MIME);
    cJSON_Delete(j);
    return is_folder;
}

success_is_true FileSystemGDrive::mkdir(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    _gdrive.ensure_access_token();

    // parent = everything before the last '/'; new folder name = the remainder
    std::string p = path;
    size_t slash = p.find_last_of('/');
    std::string parent_path = (slash != std::string::npos) ? p.substr(0, slash) : "/";
    std::string new_name = (slash != std::string::npos) ? p.substr(slash + 1) : p;

    std::string parent_id = _gdrive.resolve_path(parent_path);
    if (parent_id.empty())
        parent_id = "root";

    _last_dir[0] = '\0'; // listing changed
    RETURN_SUCCESS_IF(!_gdrive.create_folder(parent_id, new_name).empty());
}

success_is_true FileSystemGDrive::rmdir(const char *path)
{
    // A folder is deleted the same way as a file in Drive.
    return remove(path);
}

bool FileSystemGDrive::dir_exists(const char *path)
{
    return is_dir(path);
}

success_is_true FileSystemGDrive::dir_open(const char *path, const char *pattern, uint16_t diropts)
{
    if (!_started)
        RETURN_ERROR_AS_FALSE();

    Debug_printf("FileSystemGDrive::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);

    if (path == nullptr)
        RETURN_ERROR_AS_FALSE();

    if (strcmp(_last_dir, path) == 0 && !_dircache.empty())
    {
        Debug_printf("Use directory cache\n");
    }
    else
    {
        Debug_printf("Fill directory cache\n");

        _dircache.clear();
        _last_dir[0] = '\0';

        if (!_gdrive.ensure_access_token())
        {
            Debug_println("FileSystemGDrive::dir_open - no access token");
            RETURN_ERROR_AS_FALSE();
        }

        // Resolve the path to a folder id ("root" for the Drive root).
        std::string folder_id = _gdrive.resolve_path(path);
        if (folder_id.empty())
            folder_id = "root";

        // List the children of this folder.
        std::string q = "'" + folder_id + "' in parents and trashed=false";
        std::string resp = _gdrive.api_get(std::string(GDRIVE_FILES_URL) +
                                           "?q=" + GoogleDriveClient::url_encode(q) +
                                           "&fields=files(" GDRIVE_FIELDS ")"
                                           "&pageSize=1000"
                                           "&orderBy=folder,name");
        if (resp.empty())
        {
            Debug_println("FileSystemGDrive::dir_open - listing request failed");
            RETURN_ERROR_AS_FALSE();
        }

        cJSON *root = cJSON_Parse(resp.c_str());
        if (!root)
        {
            Debug_println("FileSystemGDrive::dir_open - failed to parse listing");
            RETURN_ERROR_AS_FALSE();
        }

        // Remember last visited directory
        strlcpy(_last_dir, path, MAX_PATHLEN);

        cJSON *files = cJSON_GetObjectItemCaseSensitive(root, "files");
        int count = cJSON_IsArray(files) ? cJSON_GetArraySize(files) : 0;
        for (int i = 0; i < count; i++)
        {
            cJSON *entry = cJSON_GetArrayItem(files, i);
            if (!entry)
                continue;

            cJSON *trashed = cJSON_GetObjectItemCaseSensitive(entry, "trashed");
            if (trashed && cJSON_IsTrue(trashed))
                continue;

            std::string name = GoogleDriveClient::json_str(entry, "name");
            std::string mime = GoogleDriveClient::json_str(entry, "mimeType");
            std::string size_str = GoogleDriveClient::json_str(entry, "size");
            if (name.empty())
                continue;

            fsdir_entry *fs_de = &_dircache.new_entry();
            strlcpy(fs_de->filename, name.c_str(), sizeof(fs_de->filename));
            fs_de->isDir = (mime == GDRIVE_FOLDER_MIME);
            fs_de->size = size_str.empty() ? 0 : (uint32_t)atol(size_str.c_str());
            fs_de->modified_time = 0; // Drive modifiedTime not requested

            Debug_printf(" add entry: \"%s\"\t%s\n", fs_de->filename,
                         fs_de->isDir ? "DIR" : "");
        }
        cJSON_Delete(root);
    }

    // Apply pattern matching filter and sort entries
    _dircache.apply_filter(pattern, diropts);

    RETURN_SUCCESS_AS_TRUE();
}

fsdir_entry *FileSystemGDrive::dir_read()
{
    return _dircache.read();
}

void FileSystemGDrive::dir_close()
{
    // Keep the cache populated so repeat dir_open() of the same path is cheap.
}

uint16_t FileSystemGDrive::dir_tell()
{
    return _dircache.tell();
}

success_is_true FileSystemGDrive::dir_seek(uint16_t pos)
{
    return _dircache.seek(pos);
}
