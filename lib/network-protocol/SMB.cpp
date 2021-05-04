/**
 * NetworkProtocolSMB
 * 
 * Implementation
 */

#include "SMB.h"
#include "status_error_codes.h"
#include "utils.h"
#include <fcntl.h>
#include <string.h>

NetworkProtocolSMB::NetworkProtocolSMB(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSMB::ctor\n");
    smb = smb2_init_context();
    smb_url = nullptr;
}

NetworkProtocolSMB::~NetworkProtocolSMB()
{
    Debug_printf("NetworkProtocolSMB::dtor\n");
    smb2_destroy_context(smb);
    smb_url = nullptr;
    smb = nullptr;
}

void NetworkProtocolSMB::parse_url()
{
    // use mRawURL to bypass our normal URL processing.
    if (openedURL.find("SMB:") != string::npos)
    {
        openedURL[0] = 's';
        openedURL[1] = 'm';
        openedURL[2] = 'b';
    }

    if (aux1_open == 6) // directory mode, break up path and filter
    {
        filter = openedURL.substr(openedURL.find_last_of("/") + 1);
        openedURL = openedURL.substr(0, openedURL.find_last_of("/"));
    }

    if (smb_url != nullptr)
        smb2_destroy_url(smb_url);

    smb_url = smb2_parse_url(smb, openedURL.c_str());
}

bool NetworkProtocolSMB::open_file_handle()
{
    if (smb == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - no smb context. aborting.\n");
        return true;
    }

    // Determine flags
    int flags = 0;

    switch (aux1_open)
    {
    case 4:
        flags = O_RDONLY;
        break;
    case 8:
        flags = O_WRONLY | O_CREAT;
        break;
    case 9:
        flags = O_APPEND | O_CREAT;
        break;
    case 12:
        flags = O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolSMB::open_file_handle() - Uncaught aux1 %d", aux1_open);
    }

    fh = smb2_open(smb, smb_url->path, flags);

    if (fh == nullptr)
    {
        Debug_printf("NetworkProtocolSMB::open_file_handle() - SMB Error %s\n", smb2_get_error(smb));
        fserror_to_error();
        return true;
    }

    offset = 0;

    return false;
}

bool NetworkProtocolSMB::open_dir_handle()
{
    if ((smb_dir = smb2_opendir(smb, smb_url->path)) == nullptr)
    {
        filter = openedURL.substr(openedURL.find_last_of("/") + 1);
        openedURL = openedURL.substr(0, openedURL.find_last_of("/") + 1);
        parse_url();
        Debug_printf("Filter detected, - %s - pulling out.\n", filter.c_str());
        if ((smb_dir = smb2_opendir(smb, smb_url->path)) == nullptr)
        {
            Debug_printf("NetworkProtocolSMB::open_dir_handle(%s) - ERROR: %s\n", smb_url->path, smb2_get_error(smb));
            fserror_to_error();
        }
        return true;
    }

    return false;
}

bool NetworkProtocolSMB::mount(EdUrlParser *url)
{
    openedURL = url->mRawUrl;
    parse_url();

    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);

    if (login != nullptr)
    {
        smb2_set_user(smb, login->c_str());
        smb2_set_password(smb, password->c_str());

        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, login->c_str())) != 0)
        {
            Debug_printf("NetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\n", openedURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return true;
        }
    }
    else // no u/p
    {
        if ((smb_error = smb2_connect_share(smb, smb_url->server, smb_url->share, smb_url->user)) != 0)
        {
            Debug_printf("NetworkProtocolSMB::mount(%s) - could not mount, SMB2 error: %s\n", openedURL.c_str(), smb2_get_error(smb));
            fserror_to_error();
            return true;
        }
    }

    return false;
}

bool NetworkProtocolSMB::umount()
{
    if (smb == nullptr)
        return true;

    smb2_disconnect_share(smb);

    if (smb_url == nullptr)
        return true;

    smb2_destroy_url(smb_url);
    return false;
}

void NetworkProtocolSMB::fserror_to_error()
{
    switch (smb_error)
    {
    default:
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

bool NetworkProtocolSMB::read_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pread(smb, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return true;
    }

    offset += actual_len;

    return false;
}

bool NetworkProtocolSMB::read_dir_entry(char *buf, unsigned short len)
{
    if (filter.empty())
        filter = "*";

    do
    {
        ent = smb2_readdir(smb, smb_dir);

        if (ent == nullptr)
        {
            error = NETWORK_ERROR_END_OF_FILE;
            return true;
        }

        Debug_printf("NetworkProtocolSMB::read_dir_entry(%s) ", ent->name);

        if (util_wildcard_match(ent->name, filter.c_str()) == true)
        {
            Debug_printf("MATCH!\n");
            // Set filename to buffer
            strcpy(buf, ent->name);

            // Get file size/type
            fileSize = ent->st.smb2_size;
            is_directory = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;

            error = NETWORK_ERROR_SUCCESS;
            return false;
        }
        else
        {
            Debug_printf("SKIP!\n");
            continue;
        }

    } while (ent != nullptr);

    error = NETWORK_ERROR_END_OF_FILE;
    return true;
}

bool NetworkProtocolSMB::close_file_handle()
{
    smb2_close(smb, fh);
    return false;
}

bool NetworkProtocolSMB::close_dir_handle()
{
    smb2_closedir(smb, smb_dir);
    return false;
}

bool NetworkProtocolSMB::write_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = smb2_pwrite(smb, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return true;
    }

    offset += actual_len;

    return false;
}

uint8_t NetworkProtocolSMB::special_inquiry(uint8_t cmd)
{
    return 0xff;
}

bool NetworkProtocolSMB::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    if (smb2_mkdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::mkdir(%s) SMB error: %s\n", url->mRawUrl.c_str(), smb2_get_error(smb));
    }

    umount();

    return false;
}

bool NetworkProtocolSMB::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    if (smb2_rmdir(smb, smb_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolSMB::rmdir(%s) SMB error: %s\n", url->mRawUrl.c_str(), smb2_get_error(smb));
    }

    umount();

    return false;
}

bool NetworkProtocolSMB::stat()
{
    struct smb2_stat_64 st;

    int ret = smb2_stat(smb, smb_url->path, &st);

    Debug_printf("NetworkProtocolSMB::stat(%d,%s)\n", ret, smb2_get_error(smb));

    fileSize = st.smb2_size;
    if (ret == 0)
        return false;
    else
        return true;
}

bool NetworkProtocolSMB::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}