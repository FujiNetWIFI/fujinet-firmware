#include "fnFsAFP.h"

#include <cstring>
#include <new>
#include <vector>

#include "compat_string.h"
#include "../../include/debug.h"

#include "afpFile.h"
#include "afpPath.h"
#include "fnFileAFP.h"
#include "peoples_url_parser.h"

FileSystemAFP::~FileSystemAFP()
{
    _dircache.clear();
    _session.close();
}

success_is_true FileSystemAFP::start(const char *url, const char *user, const char *password)
{
    if (_started || url == nullptr || url[0] == '\0')
        RETURN_ERROR_AS_FALSE();

    auto parsed = PeoplesUrlParser::parseURL(url);
    if (parsed == nullptr || parsed->host.empty())
        RETURN_ERROR_AS_FALSE();

    AfpPath where = afp_split_path(parsed->path);
    if (where.volume.empty())
    {
        Debug_printf("FileSystemAFP::start() - \"%s\" names no volume\n", parsed->host.c_str());
        RETURN_ERROR_AS_FALSE();
    }

    std::string login = parsed->user;
    std::string secret = parsed->password;
    if (login.empty() && user != nullptr)
    {
        login = user;
        secret = password != nullptr ? password : "";
    }

    int port = parsed->port.empty() ? 0 : atoi(parsed->port.c_str());

    if (_session.open(parsed->host, port, login, secret, where.volume).is_error())
    {
        Debug_printf("FileSystemAFP::start() - \"%s\" volume \"%s\" failed: %d\n", parsed->host.c_str(),
                     where.volume.c_str(), _session.error());
        RETURN_ERROR_AS_FALSE();
    }

    _root = where.path == "/" ? "" : where.path;
    _last_dir.clear();
    _started = true;

    RETURN_SUCCESS_AS_TRUE();
}

std::string FileSystemAFP::full_path(const char *path) const
{
    std::string out = _root;
    if (path == nullptr || path[0] != '/')
        out += '/';
    if (path != nullptr)
        out += path;
    while (out.size() > 1 && out.back() == '/')
        out.pop_back();
    return out;
}

bool FileSystemAFP::exists(const char *path)
{
    AfpDirEntry e;
    return _started && path != nullptr && afp_stat(_session, full_path(path), e) == 0;
}

bool FileSystemAFP::is_dir(const char *path)
{
    AfpDirEntry e;
    return _started && path != nullptr && afp_stat(_session, full_path(path), e) == 0 && e.is_dir;
}

success_is_true FileSystemAFP::remove(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    // The other file systems take a file or a directory here.
    std::string target = full_path(path);
    AfpDirEntry e;
    if (afp_stat(_session, target, e) != 0)
        RETURN_ERROR_AS_FALSE();

    _last_dir.clear();
    RETURN_SUCCESS_IF((e.is_dir ? afp_rmdir(_session, target) : afp_remove(_session, target)) == 0);
}

success_is_true FileSystemAFP::rename(const char *pathFrom, const char *pathTo)
{
    if (!_started || pathFrom == nullptr || pathTo == nullptr)
        RETURN_ERROR_AS_FALSE();

    _last_dir.clear();
    RETURN_SUCCESS_IF(afp_rename(_session, full_path(pathFrom), full_path(pathTo)) == 0);
}

success_is_true FileSystemAFP::mkdir(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    _last_dir.clear();
    RETURN_SUCCESS_IF(afp_mkdir(_session, full_path(path)) == 0);
}

success_is_true FileSystemAFP::rmdir(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    _last_dir.clear();
    RETURN_SUCCESS_IF(afp_rmdir(_session, full_path(path)) == 0);
}

FILE *FileSystemAFP::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemAFP::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemAFP::filehandler_open(const char *path, const char *mode)
{
    if (!_started || path == nullptr)
        return nullptr;

    FileHandlerAFP *handler = new (std::nothrow) FileHandlerAFP(_session);
    if (handler == nullptr)
        return nullptr;

    if (handler->open(full_path(path), mode).is_error())
    {
        delete handler;
        return nullptr;
    }

    // A file made by this open is not in the cached listing.
    if (mode != nullptr && mode[0] != 'r')
        _last_dir.clear();

    return handler;
}
#endif

success_is_true FileSystemAFP::dir_open(const char *path, const char *pattern, uint16_t diropts)
{
    if (!_started)
        RETURN_ERROR_AS_FALSE();

    std::string target = full_path(path);

    if (target != _last_dir)
    {
        _dircache.clear();
        _last_dir.clear();

        std::vector<AfpDirEntry> entries;
        if (afp_list(_session, target, entries) != 0)
            RETURN_ERROR_AS_FALSE();

        for (const AfpDirEntry &e : entries)
        {
            if (e.name.empty() || e.name[0] == '.')
                continue;

            fsdir_entry &entry = _dircache.new_entry();
            strlcpy(entry.filename, e.name.c_str(), sizeof(entry.filename));
            entry.isDir = e.is_dir;
            entry.size = static_cast<uint32_t>(e.size);
            // lib/afp does not report times yet.
            entry.modified_time = 0;
        }

        _last_dir = target;
    }

    _dircache.apply_filter(pattern, diropts);

    RETURN_SUCCESS_AS_TRUE();
}

fsdir_entry *FileSystemAFP::dir_read()
{
    return _dircache.read();
}

void FileSystemAFP::dir_close()
{
}

uint16_t FileSystemAFP::dir_tell()
{
    return _dircache.tell();
}

success_is_true FileSystemAFP::dir_seek(uint16_t pos)
{
    return _dircache.seek(pos);
}
