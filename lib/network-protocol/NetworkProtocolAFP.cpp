#include "NetworkProtocolAFP.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include "../../include/debug.h"

#include "afpPath.h"
#include "afpStatus.h"
#include "status_error_codes.h"

// The part of a URL path below the volume.
static std::string volume_path(const std::string &url_path)
{
    return afp_split_path(url_path).path;
}

NetworkProtocolAFP::NetworkProtocolAFP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
}

NetworkProtocolAFP::~NetworkProtocolAFP()
{
    _file.close();
    _session.close();
}

fujiError_t NetworkProtocolAFP::result(int err)
{
    _afp_error = err;
    if (err == 0)
        return FUJI_ERROR::NONE;

    fserror_to_error();
    return FUJI_ERROR::UNSPECIFIED;
}

void NetworkProtocolAFP::fserror_to_error()
{
    error = afp_errno_to_status(_afp_error);
}

fujiError_t NetworkProtocolAFP::mount(PeoplesUrlParser *url)
{
    std::string volume = afp_split_path(url->path).volume;

    // AFP://host/*.* is a listing of the server, not a volume named "*.*".
    if (volume.find_first_of("*?") != std::string::npos || volume == "-")
        volume.clear();

    std::string user = url->user;
    std::string pass = url->password;
    if (user.empty() && login != nullptr)
    {
        user = *login;
        pass = password != nullptr ? *password : "";
    }

    int port = url->port.empty() ? 0 : atoi(url->port.c_str());

    if (_session.open(url->host, port, user, pass, volume).is_error())
    {
        Debug_printf("NetworkProtocolAFP::mount(%s) failed: %d\r\n", url->host.c_str(), _session.error());
        return result(_session.error());
    }

    return result(0);
}

fujiError_t NetworkProtocolAFP::umount()
{
    _file.close();
    _session.close();
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolAFP::open_file_handle()
{
    AfpOpenMode mode;

    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        mode = AfpOpenMode::Read;
        break;
    case ACCESS_MODE::WRITE:
        mode = AfpOpenMode::Write;
        break;
    case ACCESS_MODE::APPEND:
        mode = AfpOpenMode::Append;
        break;
    case ACCESS_MODE::READWRITE:
        mode = AfpOpenMode::Update;
        break;
    default:
        return result(-EINVAL);
    }

    // opened_url->path: resolve() may have swapped in the real name.
    if (_file.open(volume_path(opened_url->path), mode).is_error())
        return result(_file.error());

    return result(0);
}

fujiError_t NetworkProtocolAFP::open_dir_handle()
{
    _entries.clear();
    _next_entry = 0;

    // Above every volume, the server's volumes are the listing.
    if (afp_split_path(dir).volume.empty())
    {
        for (const std::string &name : _session.volume_names())
        {
            AfpDirEntry e;
            e.name = name;
            e.is_dir = true;
            _entries.push_back(e);
        }
        return result(0);
    }

    return result(afp_list(_session, volume_path(dir), _entries));
}

fujiError_t NetworkProtocolAFP::read_dir_entry(char *buf, unsigned short len)
{
    if (_next_entry >= _entries.size())
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    const AfpDirEntry &e = _entries[_next_entry++];
    snprintf(buf, len, "%s", e.name.c_str());
    fileSize = e.size;
    is_directory = e.is_dir;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolAFP::close_dir_handle()
{
    _entries.clear();
    _entries.shrink_to_fit();
    _next_entry = 0;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolAFP::read_file_handle(uint8_t *buf, unsigned short len)
{
    // The base wants exactly len bytes; AfpFile may return fewer per call.
    unsigned short got = 0;
    while (got < len)
    {
        int n = _file.read(buf + got, len - got);
        if (n < 0)
            return result(n);
        if (n == 0)
        {
            error = NDEV_STATUS::END_OF_FILE;
            return FUJI_ERROR::UNSPECIFIED;
        }
        got += n;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolAFP::write_file_handle(uint8_t *buf, unsigned short len)
{
    unsigned short put = 0;
    while (put < len)
    {
        int n = _file.write(buf + put, len - put);
        if (n < 0)
            return result(n);
        // Nothing written and no error would loop for ever.
        if (n == 0)
            return result(-EIO);
        put += n;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolAFP::close_file_handle()
{
    if (_file.close().is_error())
        return result(_file.error());

    return result(0);
}

fujiError_t NetworkProtocolAFP::stat()
{
    AfpDirEntry e;
    int err = afp_stat(_session, volume_path(opened_url->path), e);
    if (err != 0)
    {
        // resolve() probes with this and expects misses, so error is left alone.
        _afp_error = err;
        return FUJI_ERROR::UNSPECIFIED;
    }

    fileSize = e.size;
    return FUJI_ERROR::NONE;
}

off_t NetworkProtocolAFP::seek(off_t position, int whence)
{
    // Signed, so a position before the start can be seen and held at 0.
    int64_t size = _file.size();
    int64_t offset = _file.tell();

    if (whence == SEEK_SET)
        offset = position;
    else if (whence == SEEK_CUR)
        offset += position;
    else if (whence == SEEK_END)
        offset = size - position;

    if (offset < 0)
        offset = 0;

    _file.seek(offset);
    // fileSize is bytes remaining; past the end there are none.
    fileSize = offset < size ? size - offset : 0;
    receiveBuffer->clear();

    return offset;
}

template <typename Call> fujiError_t NetworkProtocolAFP::one_shot(PeoplesUrlParser *url, Call call)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    fujiError_t err = result(call());
    umount();
    return err;
}

fujiError_t NetworkProtocolAFP::rename(PeoplesUrlParser *url)
{
    // The base splits "dir/OLD,NEW" into filename and destFilename.
    if (NetworkProtocolFS::rename(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    return one_shot(url, [&] { return afp_rename(_session, volume_path(filename), volume_path(destFilename)); });
}

fujiError_t NetworkProtocolAFP::del(PeoplesUrlParser *url)
{
    return one_shot(url, [&] { return afp_remove(_session, volume_path(url->path)); });
}

fujiError_t NetworkProtocolAFP::mkdir(PeoplesUrlParser *url)
{
    return one_shot(url, [&] { return afp_mkdir(_session, volume_path(url->path)); });
}

fujiError_t NetworkProtocolAFP::rmdir(PeoplesUrlParser *url)
{
    return one_shot(url, [&] { return afp_rmdir(_session, volume_path(url->path)); });
}
