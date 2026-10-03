/**
 * NetworkProtocolSMB
 *
 * Implementation
 */

#include "SMB.h"

#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <algorithm>

#include "../../include/debug.h"

#include <smb2/libsmb2.h>
#include <smb2/smb2.h>
//#include <smb2/libsmb2-raw.h>

#include "status_error_codes.h"
#include "utils.h"

#include <vector>
#include <algorithm>

NetworkProtocolSMB::NetworkProtocolSMB(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSMB::ctor\r\n");
    smb = smb2_init_context();
}

NetworkProtocolSMB::~NetworkProtocolSMB()
{
    Debug_printf("NetworkProtocolSMB::dtor\r\n");
    smb2_destroy_context(smb);
}

fujiError_t NetworkProtocolSMB::open_file_handle()
{
    if (smb == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - no smb context. aborting.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Determine flags
    int flags = 0;

    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        flags = O_RDONLY;
        break;
    case ACCESS_MODE::WRITE:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case ACCESS_MODE::APPEND:
        flags = O_APPEND | O_CREAT;
        break;
    case ACCESS_MODE::READWRITE:
        flags = O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolSMB::open_file_handle() - Uncaught aux1 %d", (int) streamMode);
    }

    // opened_url->path, not smb_url->path: resolve() may have swapped in the real name.
    fh = smb2_open(smb, share_path(opened_url->path).c_str(), flags);

    if (fh == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - SMB Error %s\r\n", smb2_get_error(smb));
        smb_error = -nterror_to_errno(smb2_get_nterror(smb));
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset = 0;

    Debug_printf("DO WE FUCKING GET HERE?!\r\n");

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::open_dir_handle()
{
    // resolve() lists the directory of the file it opens.
    std::string path = streamMode == ACCESS_MODE::DIRECTORY || streamMode == ACCESS_MODE::DIRECTORY_ALT
                           ? smb_url->path
                           : share_path(dir);

    if ((smb_dir = smb2_opendir(smb, path.c_str())) == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_dir_handle() - ERROR: %s\r\n", smb2_get_error(smb));
        smb_error = -nterror_to_errno(smb2_get_nterror(smb));
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

std::string lowercase_if_no_lowercase(std::string s)
{
    if (!std::any_of(s.begin(), s.end(),
                     [](unsigned char c) { return std::islower(c); }))
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    return s;
}

fujiError_t NetworkProtocolSMB::mount(PeoplesUrlParser *url)
{
    std::string openURL = url->url;

    // use mRawURL to bypass our normal URL processing.
    if (openURL.find("SMB:") != std::string::npos)
    {
        openURL[0] = 's';
        openURL[1] = 'm';
        openURL[2] = 'b';
    }

    if (streamMode == ACCESS_MODE::DIRECTORY)
    {
        // When doing a directory listing the Atari DIR command sends
        // the directory path followed by `/<glob>` (usually "*.*")
        // which needs to be removed.
        std::size_t pos = openURL.find_last_of("/");
        if (pos < std::string::npos)
        {
            std::string lastComponent = openURL.substr(pos + 1);
            if (lastComponent.find('*') != std::string::npos ||
                lastComponent.find('?') != std::string::npos)
                openURL = openURL.substr(0, pos);
        }
    }

    Debug_printf("NetworkProtocolSMB::mount() - openURL: %s\r\n", openURL.c_str());
    smb_url = smb2_parse_url(smb, openURL.c_str());
    if (smb_url == nullptr)
    {
        Debug_printf("aNetworkProtocolSMB::mount(%s) - failed to parse URL, SMB2 error: %s\n", openURL.c_str(), smb2_get_error(smb));
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);

    if (smb_url->user || login != nullptr)
    {
        std::string user, pass;

        if (smb_url->user)
        {
            std::string_view up(smb_url->user);
            auto pos = up.find(':');
            user.assign(up.substr(0, pos));
            if (pos != std::string_view::npos)
                pass.assign(up.substr(pos + 1));

            user = lowercase_if_no_lowercase(user);
            pass = lowercase_if_no_lowercase(pass);
        }
        else
        {
            user = *login;
            pass = *password;
        }

        smb2_set_user(smb, user.c_str());
        smb2_set_password(smb, pass.c_str());

        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, user.c_str())) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\r\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
    }
    else // no u/p
    {
        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, smb_url->user)) != 0)
        {
            Debug_printf("aNetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\r\n", openURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::umount()
{
    if (smb == nullptr)
        return FUJI_ERROR::UNSPECIFIED;

    smb2_disconnect_share(smb);

    if (smb_url == nullptr)
        return FUJI_ERROR::UNSPECIFIED;

    smb2_destroy_url(smb_url);
    return FUJI_ERROR::NONE;
}

void NetworkProtocolSMB::fserror_to_error()
{
    // libsmb2 returns -errno.
    switch (smb_error)
    {
    case -ENOENT:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    case -EEXIST:
        error = NDEV_STATUS::FILE_EXISTS;
        break;
    case -EACCES:
    case -EPERM:
    case -EROFS:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case -ENOSPC:
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        break;
    default:
        error = NDEV_STATUS::GENERAL;
        break;
    }
}

fujiError_t NetworkProtocolSMB::read_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pread(smb, fh, buf, len, offset)) != len)
    {
        smb_error = actual_len < 0 ? actual_len : 0;
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset += actual_len;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::read_dir_entry(char *buf, unsigned short len)
{
    ent = smb2_readdir(smb, smb_dir);

    if (ent == nullptr)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Set filename to buffer
    strcpy(buf, ent->name);

    // Get file size/type
    fileSize = ent->st.smb2_size;
    is_directory = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::close_file_handle()
{
    smb2_close(smb, fh);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::close_dir_handle()
{
    smb2_closedir(smb, smb_dir);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::write_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pwrite(smb, fh, buf, len, offset)) != len)
    {
        smb_error = actual_len < 0 ? actual_len : 0;
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset += actual_len;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::rename(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    // smb_url->path is relative to the share: dir/OLD,NEW, both in dir.
    std::string path = smb_url->path ? smb_url->path : "";
    size_t comma = path.find(',');
    if (comma == std::string::npos)
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        umount();
        return FUJI_ERROR::UNSPECIFIED;
    }

    size_t slash = path.find_last_of('/', comma);
    std::string from = path.substr(0, comma);
    std::string to = (slash == std::string::npos ? "" : path.substr(0, slash + 1)) + path.substr(comma + 1);

    if ((smb_error = smb2_rename(smb, from.c_str(), to.c_str())) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::rename(%s) SMB error: %s\r\n", url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return smb_error != 0 ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::del(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    if ((smb_error = smb2_unlink(smb, smb_url->path)) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::del(%s) SMB error: %s\r\n", url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return smb_error != 0 ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::mkdir(PeoplesUrlParser *url)
{
    mount(url);

    if ((smb_error = smb2_mkdir(smb, smb_url->path)) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::mkdir(%s) SMB error: %s\r\n",url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return smb_error != 0 ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::rmdir(PeoplesUrlParser *url)
{
    mount(url);

    if ((smb_error = smb2_rmdir(smb, smb_url->path)) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::rmdir(%s) SMB error: %s\r\n",url->url.c_str(), smb2_get_error(smb));
    }

    umount();

    return smb_error != 0 ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSMB::stat()
{
    struct smb2_stat_64 st;

    int ret = smb2_stat(smb, share_path(opened_url->path).c_str(), &st);

    fileSize = st.smb2_size;
    return ret != 0 ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

std::string NetworkProtocolSMB::share_path(const std::string &path)
{
    size_t slash = path.find('/', path.find_first_not_of('/'));
    if (slash == std::string::npos)
        return "";

    std::string rel = path.substr(slash + 1);
    while (!rel.empty() && rel.back() == '/')
        rel.pop_back();
    return rel;
}

off_t NetworkProtocolSMB::seek(off_t position, int whence)
{
    // fileSize isn't fileSize, it's bytes remaining. Call stat() to fix fileSize
    stat();

    if (whence == SEEK_SET)
        offset = position;
    else if (whence == SEEK_CUR)
        offset += position;
    else if (whence == SEEK_END)
        offset = fileSize - position;

    fileSize -= offset;
    receiveBuffer->clear();

    return offset;
}
