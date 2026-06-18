
#include "fnFsGDrive.h"

#include <cstring>
#include <cstdio>
#include <ctime>

#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFileCache.h"
#include "fnFsSD.h"

// http timeout in ms while streaming a download
#define GDRIVE_GET_TIMEOUT 20000

#define COPY_BLK_SIZE 4096

#ifdef FNIO_IS_STDIO
// On stdio targets (e.g. ADAM) there is no FileHandler/FileCache; Drive files
// are cached to the SD card and served as a FILE*.
#define GDRIVE_CACHE_DIR        "/FujiNet/cache"
// Cached file is reused if it is younger than this (seconds), like fnFileCache.
#define GDRIVE_CACHE_MAX_AGE    10800
#endif

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

// Upload `len` bytes pulled from read_chunk() to Drive at `path` (create or
// update), then clear the dirty flag on success.
success_is_true FileSystemGDrive::upload_path(const char *path, size_t len,
        const std::function<int(uint8_t *, int)> &read_chunk)
{
    if (!_gdrive.ensure_access_token())
        RETURN_ERROR_AS_FALSE();

    // Split path into parent folder + file name.
    std::string p = path;
    size_t slash = p.find_last_of('/');
    std::string parent_path = (slash != std::string::npos) ? p.substr(0, slash) : "/";
    std::string name = (slash != std::string::npos) ? p.substr(slash + 1) : p;
    if (name.empty())
        RETURN_ERROR_AS_FALSE();

    std::string parent_id = _gdrive.resolve_path(parent_path);
    if (parent_id.empty())
        parent_id = "root";
    std::string file_id = _gdrive.find_child(parent_id, name, false); // "" => create new

    Debug_printf("FileSystemGDrive::sync_file uploading %s (%u bytes)\n", path, (unsigned)len);

    std::string id = _gdrive.upload_stream(parent_id, name, file_id, len, read_chunk);
    if (id.empty())
    {
        Debug_printf("FileSystemGDrive::sync_file - upload failed for %s\n", path);
        RETURN_ERROR_AS_FALSE();
    }

    _dirty.erase(path);
    _last_dir[0] = '\0'; // directory listing may have changed
    Debug_printf("FileSystemGDrive::sync_file - uploaded %s\n", path);
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true FileSystemGDrive::sync_file(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    // Only upload files that were opened for writing in this session.
    if (_dirty.find(path) == _dirty.end())
        RETURN_SUCCESS_AS_TRUE(); // nothing to push back

#ifdef FNIO_IS_STDIO
    // stdio: the image was written straight to an SD cache FILE*; read it back.
    if (!fnSDFAT.running())
        RETURN_ERROR_AS_FALSE();

    std::string cache_path = cache_file_path(path);
    FILE *in = fnSDFAT.file_open(cache_path.c_str(), "rb");
    if (in == nullptr)
    {
        Debug_printf("FileSystemGDrive::sync_file - no cache file for %s\n", path);
        _dirty.erase(path);
        RETURN_ERROR_AS_FALSE();
    }

    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (sz < 0)
    {
        fclose(in);
        RETURN_ERROR_AS_FALSE();
    }

    success_is_true ok = upload_path(path, (size_t)sz,
        [in](uint8_t *buf, int want) -> int {
            return (int)fread(buf, 1, (size_t)want, in);
        });
    fclose(in);
    return ok;
#else
    // FileHandler/FileCache: the writable cache was forced onto SD (see
    // cache_file), so reopen it for reading and upload.
    FileHandler *in = FileCache::open(_rawurl.c_str(), path, "rb");
    if (in == nullptr)
    {
        Debug_printf("FileSystemGDrive::sync_file - no cache file for %s\n", path);
        _dirty.erase(path);
        RETURN_ERROR_AS_FALSE();
    }

    in->seek(0, SEEK_END);
    long sz = in->tell();
    in->seek(0, SEEK_SET);
    if (sz < 0)
    {
        in->close();
        RETURN_ERROR_AS_FALSE();
    }

    success_is_true ok = upload_path(path, (size_t)sz,
        [in](uint8_t *buf, int want) -> int {
            return (int)in->read(buf, 1, (size_t)want);
        });
    in->close();
    return ok;
#endif
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
#ifdef FNIO_IS_STDIO
    // stdio targets (e.g. ADAM) have no FileHandler/FileCache. Drive files are
    // cached to the SD card and served as a FILE*.
    //
    // NOTE: writes/creates are LOCAL ONLY for now — a newly created or modified
    // image lives in the SD cache (so it can be created and used within this
    // session) but is NOT uploaded back to Google Drive yet. Pushing changes
    // back needs an upload on disk-image unmount (TODO).
    Debug_printf("FileSystemGDrive::file_open(\"%s\", \"%s\")\n", path, mode);

    if (!_started || path == nullptr || mode == nullptr)
        return nullptr;

    if (!fnSDFAT.running())
    {
        Debug_println("FileSystemGDrive::file_open - SD card required to cache Drive files");
        return nullptr;
    }

    const bool truncating = (mode[0] == 'w');                  // create/overwrite
    const bool writing    = (strpbrk(mode, "wa+") != nullptr); // any write intent

    // Remember write-intent opens so the image is uploaded back to Drive when
    // the disk is unmounted (see sync_file()).
    if (writing)
        _dirty.insert(path);

    std::string cache_path = cache_file_path(path);

    // Reuse a recent cache file if present (avoids re-downloading on re-mount,
    // and lets a just-created local image be mounted in this session). Skip for
    // 'w' which is meant to start from an empty file.
    if (!truncating)
    {
        long mt = fnSDFAT.mtime(cache_path.c_str());
        if (mt > 0 && (time(nullptr) - mt) < GDRIVE_CACHE_MAX_AGE)
        {
            FILE *cached = fnSDFAT.file_open(cache_path.c_str(), mode);
            if (cached != nullptr)
            {
                Debug_printf("FileSystemGDrive: using cached file %s\n", cache_path.c_str());
                return cached;
            }
        }
    }

    if (!_gdrive.ensure_access_token())
    {
        Debug_println("FileSystemGDrive::file_open - no access token");
        return nullptr;
    }

    // 'w'/'w+' starts from an empty local file regardless of what's on Drive.
    if (truncating)
    {
        Debug_printf("FileSystemGDrive: creating local-only file %s (not uploaded to Drive)\n", path);
        fnSDFAT.create_path(GDRIVE_CACHE_DIR);
        return fnSDFAT.file_open(cache_path.c_str(), mode);
    }

    std::string file_id = _gdrive.resolve_path(path);
    if (file_id.empty())
    {
        // Nothing on Drive. For append-create, start a new local file; for a
        // pure read this is a genuine "not found".
        if (writing)
        {
            Debug_printf("FileSystemGDrive: creating local-only file %s (not uploaded to Drive)\n", path);
            fnSDFAT.create_path(GDRIVE_CACHE_DIR);
            return fnSDFAT.file_open(cache_path.c_str(), mode);
        }
        Debug_printf("FileSystemGDrive::file_open - not found: %s\n", path);
        return nullptr;
    }

    // File exists on Drive — download it to the cache, then open in the
    // requested mode (read, or read/write for local-only edits).
    fnSDFAT.create_path(GDRIVE_CACHE_DIR);
    FILE *out = fnSDFAT.file_open(cache_path.c_str(), "wb");
    if (out == nullptr)
    {
        Debug_printf("FileSystemGDrive::file_open - cannot create cache file %s\n", cache_path.c_str());
        return nullptr;
    }

    bool ok = stream_download(file_id, [out](const uint8_t *data, int len) -> bool {
        return fwrite(data, 1, len, out) == (size_t)len;
    });
    fclose(out);

    if (!ok)
    {
        fnSDFAT.remove(cache_path.c_str());
        return nullptr;
    }

    Debug_printf("FileSystemGDrive: cached %s\n", cache_path.c_str());
    return fnSDFAT.file_open(cache_path.c_str(), mode);
#else
    Debug_printf("FileSystemGDrive::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
#endif
}

// Stream a Drive file's content (?alt=media) for the given file id, passing
// each chunk to sink(). Returns true on a complete, successful download.
bool FileSystemGDrive::stream_download(const std::string &file_id,
                                       const std::function<bool(const uint8_t *, int)> &sink)
{
    GDFS_HTTP_CLIENT http;
    std::string dl_url = std::string(GDRIVE_FILES_URL) + "/" + file_id + "?alt=media";
    if (!http.begin(dl_url))
    {
        Debug_println("FileSystemGDrive::stream_download - failed to start HTTP client");
        return false;
    }
    http.set_header("Authorization", ("Bearer " + _gdrive.access_token()).c_str());

    Debug_println("Initiating GET request");
    if (http.GET() > 399)
    {
        Debug_println("FileSystemGDrive::stream_download - GET failed");
        http.close();
        return false;
    }

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
        Debug_println("FileSystemGDrive::stream_download - failed to allocate buffer");
        http.close();
        return false;
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
                Debug_println("FileSystemGDrive::stream_download - Timeout");
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
                    Debug_println("FileSystemGDrive::stream_download - HTTP read failed");
                    Debug_printf("  Expected %d bytes, actually got %d bytes.\r\n", to_read, from_read);
                    cancel = true;
                    break;
                }
                if (!sink(buf, to_read))
                {
                    Debug_printf("FileSystemGDrive::stream_download - sink write failed\n");
                    cancel = true;
                    break;
                }
                available = http.available();
            }
            tmout_counter = 1 + GDRIVE_GET_TIMEOUT / 50; // reset timeout counter
        }
        else // available < 0
        {
            Debug_println("FileSystemGDrive::stream_download - something went wrong");
            cancel = true;
        }
    }
    free(buf);
    http.close();

    return !cancel;
}

#ifdef FNIO_IS_STDIO
std::string FileSystemGDrive::cache_file_path(const char *path)
{
    // Deterministic, filesystem-safe name derived from host + path so the same
    // Drive file maps to the same cache file (and distinct files don't collide).
    std::hash<std::string> hasher;
    unsigned long h1 = (unsigned long)hasher(_rawurl);
    unsigned long h2 = (unsigned long)hasher(std::string(path));
    char name[64];
    snprintf(name, sizeof(name), "%s/gd-%08lx%08lx.tmp", GDRIVE_CACHE_DIR, h1, h2);
    return std::string(name);
}
#endif

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

    const bool truncating = (mode[0] == 'w');                  // create/overwrite
    const bool writing    = (strpbrk(mode, "wa+") != nullptr); // any write intent

    // Remember write-intent opens so the image is uploaded back to Drive when
    // the disk is unmounted (see sync_file()).
    if (writing)
        _dirty.insert(path);

    // Try the SD cache first (skip for 'w' which should start from empty).
    if (!truncating)
    {
        FileHandler *fh = FileCache::open(_rawurl.c_str(), path, mode);
        if (fh != nullptr)
            return fh; // cache hit, done
    }

    if (!_gdrive.ensure_access_token())
    {
        Debug_println("FileSystemGDrive::cache_file - no access token");
        return nullptr;
    }

    // Decide whether to seed the cache with the file's current Drive content.
    // 'w'/'w+' always starts empty; otherwise download the existing file.
    std::string file_id;
    if (!truncating)
    {
        file_id = _gdrive.resolve_path(path);
        if (file_id.empty() && !writing)
        {
            Debug_printf("FileSystemGDrive::cache_file - not found: %s\n", path);
            return nullptr;
        }
    }

    // For write modes force the cache onto the SD card (threshold 0) so the disk
    // device's writes land in a real file that survives the handle being closed
    // and can be read back for upload on unmount. Reads keep the faster
    // memory-first cache.
    const int threshold = writing ? 0 : -1;

    fc_handle *fc = FileCache::create(_rawurl.c_str(), path, threshold);
    if (fc == nullptr)
        return nullptr;

    // A zero-length write makes the (threshold 0) cache switch to SD right away,
    // even when there is no Drive content to download (e.g. a brand-new image).
    if (writing)
        FileCache::write(fc, "", 0);

    if (!file_id.empty())
    {
        bool ok = stream_download(file_id, [fc](const uint8_t *data, int len) -> bool {
            return FileCache::write(fc, data, len) == (size_t)len;
        });
        if (!ok)
        {
            FileCache::remove(fc);
            return nullptr;
        }
    }

    return FileCache::reopen(fc, mode);
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
