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

FileSystemAFP::FileSystemAFP() : _session(new (std::nothrow) AfpSession)
{
}

// The session closes when the last open file lets go of it.
FileSystemAFP::~FileSystemAFP()
{
}

success_is_true FileSystemAFP::start(const char *url, const char *user, const char *password)
{
    if (_started || _session == nullptr || url == nullptr || url[0] == '\0')
        RETURN_ERROR_AS_FALSE();

    PeoplesUrlParser parsed(url);
    if (parsed.host.empty())
        RETURN_ERROR_AS_FALSE();

    AfpPath where = afp_split_path(parsed.path);
    if (where.volume.empty())
    {
        Debug_printf("FileSystemAFP::start() - \"%s\" names no volume\n", parsed.host.c_str());
        RETURN_ERROR_AS_FALSE();
    }

    std::string login = parsed.user;
    std::string secret = parsed.password;
    if (login.empty() && user != nullptr)
    {
        login = user;
        secret = password != nullptr ? password : "";
    }

    int port = parsed.port.empty() ? 0 : atoi(parsed.port.c_str());

    if (_session->open(parsed.host, port, login, secret, where.volume).is_error())
    {
        Debug_printf("FileSystemAFP::start() - \"%s\" volume \"%s\" failed: %d\n", parsed.host.c_str(),
                     where.volume.c_str(), _session->error());
        RETURN_ERROR_AS_FALSE();
    }

    _root = where.path == "/" ? "" : where.path;
    drop_listing();
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
    return _started && path != nullptr && afp_stat(*_session, full_path(path), e) == 0;
}

bool FileSystemAFP::is_dir(const char *path)
{
    AfpDirEntry e;
    return _started && path != nullptr && afp_stat(*_session, full_path(path), e) == 0 && e.is_dir;
}

success_is_true FileSystemAFP::remove(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    // The other file systems take a file or a directory here.
    std::string target = full_path(path);
    AfpDirEntry e;
    if (afp_stat(*_session, target, e) != 0)
        RETURN_ERROR_AS_FALSE();

    drop_listing();
    RETURN_SUCCESS_IF((e.is_dir ? afp_rmdir(*_session, target) : afp_remove(*_session, target)) == 0);
}

success_is_true FileSystemAFP::rename(const char *pathFrom, const char *pathTo)
{
    if (!_started || pathFrom == nullptr || pathTo == nullptr)
        RETURN_ERROR_AS_FALSE();

    drop_listing();
    RETURN_SUCCESS_IF(afp_rename(*_session, full_path(pathFrom), full_path(pathTo)) == 0);
}

success_is_true FileSystemAFP::mkdir(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    drop_listing();
    RETURN_SUCCESS_IF(afp_mkdir(*_session, full_path(path)) == 0);
}

success_is_true FileSystemAFP::rmdir(const char *path)
{
    if (!_started || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    drop_listing();
    RETURN_SUCCESS_IF(afp_rmdir(*_session, full_path(path)) == 0);
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
        drop_listing();

    return handler;
}
#endif

success_is_true FileSystemAFP::fill_dircache(const char *path)
{
    std::vector<AfpDirEntry> entries;
    if (afp_list(*_session, full_path(path), entries) != 0)
        RETURN_ERROR_AS_FALSE();

    for (const AfpDirEntry &e : entries)
    {
        if (e.name.empty() || e.name[0] == '.')
            continue;

        // lib/afp does not report times yet.
        if (_dircache->add_entry(e.name.c_str(), e.is_dir, static_cast<uint32_t>(e.size), 0).is_error())
            RETURN_ERROR_AS_FALSE();
    }

    RETURN_SUCCESS_AS_TRUE();
}
