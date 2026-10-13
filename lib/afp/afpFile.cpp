#include "afpFile.h"

#include <algorithm>
#include <cerrno>

#include "afpBridge.h"

namespace
{
int bridge_mode(AfpOpenMode mode)
{
    switch (mode)
    {
    case AfpOpenMode::Write:
        return AFP_BRIDGE_WRITE;
    case AfpOpenMode::Append:
        return AFP_BRIDGE_APPEND;
    case AfpOpenMode::Update:
        return AFP_BRIDGE_UPDATE;
    case AfpOpenMode::Read:
    default:
        return AFP_BRIDGE_READ;
    }
}

std::string leaf(const std::string &path)
{
    return path.substr(path.find_last_of('/') + 1);
}
} // namespace

AfpFile::~AfpFile()
{
    close();
}

success_is_true AfpFile::open(const std::string &path, AfpOpenMode mode)
{
    close();
    _error = 0;
    _offset = 0;
    _size = 0;

    struct afp_volume *volume = _session.volume();
    if (volume == nullptr)
    {
        _error = -ENOTCONN;
        RETURN_ERROR_AS_FALSE();
    }

    _error = afp_bridge_open(volume, path.c_str(), bridge_mode(mode), &_file);
    if (_error != 0)
    {
        _file = nullptr;
        RETURN_ERROR_AS_FALSE();
    }
    _path = path;

    if (mode != AfpOpenMode::Write)
    {
        int is_dir = 0;
        unsigned long long size = 0;
        if (afp_bridge_stat(volume, path.c_str(), &is_dir, &size) == 0)
        {
            // Not every server refuses to open a directory as a file.
            if (is_dir)
            {
                afp_bridge_close(volume, path.c_str(), _file);
                _file = nullptr;
                _error = -EISDIR;
                RETURN_ERROR_AS_FALSE();
            }
            _size = size;
        }
        if (mode == AfpOpenMode::Append)
            _offset = _size;
    }
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true AfpFile::close()
{
    if (_file == nullptr)
        RETURN_SUCCESS_AS_TRUE();

    // Once the library has unmounted the volume it has freed this handle too.
    struct afp_volume *volume = _session.volume();
    _error = volume ? afp_bridge_close(volume, _path.c_str(), _file) : 0;
    _file = nullptr;
    RETURN_SUCCESS_IF(_error == 0);
}

int AfpFile::read(void *buf, size_t len)
{
    if (_file == nullptr)
        return -EBADF;
    struct afp_volume *volume = _session.volume();
    if (volume == nullptr)
        return -ENOTCONN;

    int n = afp_bridge_read(volume, _path.c_str(), _file, buf, static_cast<unsigned int>(len), _offset);
    if (n > 0)
        _offset += static_cast<uint64_t>(n);
    return n;
}

int AfpFile::write(const void *buf, size_t len)
{
    if (_file == nullptr)
        return -EBADF;
    struct afp_volume *volume = _session.volume();
    if (volume == nullptr)
        return -ENOTCONN;

    int n = afp_bridge_write(volume, _path.c_str(), _file, buf, static_cast<unsigned int>(len), _offset);
    if (n > 0)
    {
        _offset += static_cast<uint64_t>(n);
        _size = std::max(_size, _offset);
    }
    return n;
}

int afp_stat(AfpSession &session, const std::string &path, AfpDirEntry &out)
{
    struct afp_volume *volume = session.volume();
    if (volume == nullptr)
        return -ENOTCONN;

    int is_dir = 0;
    unsigned long long size = 0;
    int ret = afp_bridge_stat(volume, path.c_str(), &is_dir, &size);
    if (ret != 0)
        return ret;
    out.name = leaf(path);
    out.is_dir = is_dir != 0;
    out.size = size;
    return 0;
}

int afp_list(AfpSession &session, const std::string &path, std::vector<AfpDirEntry> &out)
{
    out.clear();
    struct afp_volume *volume = session.volume();
    if (volume == nullptr)
        return -ENOTCONN;

    struct afp_file_info *entries = nullptr;
    int ret = afp_bridge_list(volume, path.c_str(), &entries);
    if (ret != 0)
        return ret;

    for (const struct afp_file_info *e = entries; e != nullptr; e = afp_bridge_entry_next(e))
    {
        AfpDirEntry entry;
        entry.name = afp_bridge_entry_name(e);
        entry.is_dir = afp_bridge_entry_is_dir(e) != 0;
        entry.size = afp_bridge_entry_size(e);
        out.push_back(std::move(entry));
    }
    afp_bridge_list_free(entries);
    return 0;
}

int afp_remove(AfpSession &session, const std::string &path)
{
    struct afp_volume *volume = session.volume();
    return volume ? afp_bridge_remove(volume, path.c_str()) : -ENOTCONN;
}

int afp_rename(AfpSession &session, const std::string &from, const std::string &to)
{
    struct afp_volume *volume = session.volume();
    return volume ? afp_bridge_rename(volume, from.c_str(), to.c_str()) : -ENOTCONN;
}

int afp_mkdir(AfpSession &session, const std::string &path)
{
    struct afp_volume *volume = session.volume();
    return volume ? afp_bridge_mkdir(volume, path.c_str()) : -ENOTCONN;
}

int afp_rmdir(AfpSession &session, const std::string &path)
{
    struct afp_volume *volume = session.volume();
    return volume ? afp_bridge_rmdir(volume, path.c_str()) : -ENOTCONN;
}
