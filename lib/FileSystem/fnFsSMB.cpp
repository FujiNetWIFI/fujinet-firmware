
#include "fnFsSMB.h"

#include <fcntl.h>
#include <errno.h>
#include "compat_string.h"

#include "../../include/debug.h"

#include "smb2/smb2.h"
#include "fnFileSMB.h"

FileSystemSMB::FileSystemSMB()
{
    Debug_printf("FileSystemSMB::ctor\n");
    _smb = nullptr;
    _url = nullptr;
    // invalidate _last_dir
    _last_dir[0] = '/';
    _last_dir[1] = '\0';
}

FileSystemSMB::~FileSystemSMB()
{
    Debug_printf("FileSystemSMB::dtor\n");
    if (_started)
    {
        _dircache.clear();
        smb2_disconnect_share(_smb);
        smb2_destroy_url(_url);
        smb2_destroy_context(_smb);
    }
}

bool FileSystemSMB::start(const char *url, const char *user, const char *password)
{
    int smb_error;

    if (_started)
        return false;

    if(url == nullptr || url[0] == '\0')
        return false;

    _smb = smb2_init_context();
    if (_smb == nullptr) 
    {
        Debug_printf("FileSystemSMB::start() - failed to init SMB2 context\n");
        return false;
    }

    _url = smb2_parse_url(_smb, url);
    if (_url == nullptr) 
    {
        Debug_printf("FileSystemSMB::start() - failed to parse URL \"%s\", SMB2 error: %s\n", url, smb2_get_error(_smb));
        return false;
    }

    smb2_set_security_mode(_smb, SMB2_NEGOTIATE_SIGNING_ENABLED);

    if (user != nullptr && password != nullptr)
    {
        smb2_set_user(_smb, user);
        smb2_set_password(_smb, password);
        smb_error = smb2_connect_share(_smb, _url->server, _url->share, user);
    }
    else
    {
        smb_error = smb2_connect_share(_smb, _url->server, _url->share, _url->user);
    }

	if (smb_error != 0) 
    {
        Debug_printf("FileSystemSMB::start() - failed to connect share \"//%s/%s\", SMB2 error: %s\n", _url->server, _url->share, smb2_get_error(_smb));
        return false;
	}

    Debug_printf("SMB share connected: //%s/%s\n", _url->server, _url->share);

    _started = true;

    return true;
}

bool FileSystemSMB::exists(const char *path)
{
    smb2_stat_64 st;
    int smb_error = smb2_stat(_smb, path, &st);

    return smb_error == 0;
}

bool FileSystemSMB::remove(const char *path)
{
    if(path == nullptr)
        return false;

    // Figure out if this is a file or directory
    smb2_stat_64 st;
    if (0 != smb2_stat(_smb, path, &st))
        return false;

    int smb_error;
    if (st.smb2_type == SMB2_TYPE_DIRECTORY)
        smb_error = smb2_rmdir(_smb, path);
    else
        smb_error = smb2_unlink(_smb, path);

    if (smb_error != 0)
        Debug_printf("FileSystemSMB::remove(\"%s\") - failed, SMB2 error: %s\n", path, smb2_get_error(_smb));

    return smb_error == 0;
}

bool FileSystemSMB::rename(const char *pathFrom, const char *pathTo)
{
    int smb_error = smb2_rename(_smb, pathFrom, pathTo);
    return smb_error == 0;    
}

FILE  *FileSystemSMB::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemSMB::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemSMB::filehandler_open(const char *path, const char *mode)
{
    if(!_started || path == nullptr)
        return nullptr;

    // skip '/' at beginning
    const char *smb_path = path;
    if (smb_path != nullptr && smb_path[0] == '/')
        smb_path += 1;

    struct smb2fh *fh;
    int open_flags = 0;

    // TODO check/fix open mode translation
    for (const char *m = mode; *m != '\0'; m++)
    {
        switch (*m)
        {
        case 'r':
            open_flags = O_RDONLY;
            break;
        case 'w':
            open_flags = O_WRONLY | O_CREAT;
            break;
        case 'a':
            open_flags = O_WRONLY;
            break;
        case '+':
            // TODO
            if (open_flags == O_RDONLY) // "r+""
                open_flags = O_RDWR;
            else if (open_flags == (O_WRONLY | O_CREAT)) // "w+"
                open_flags = O_RDWR | O_CREAT;
            else if (open_flags == O_WRONLY) // "a+"
                open_flags = O_RDWR | O_CREAT;
            break;
        }
    }

    if ((fh = smb2_open(_smb, smb_path, O_RDONLY)) == nullptr) // TODO use open_flags
    {
        return nullptr;
    }

    return new FileHandlerSMB(_smb, fh);
}
#endif

bool FileSystemSMB::is_dir(const char *path)
{
    smb2_stat_64 st;
    if (smb2_stat(_smb, path, &st) != 0)
        return false;
    return st.smb2_type == SMB2_TYPE_DIRECTORY;
}

bool FileSystemSMB::dir_open(const char  *path, const char *pattern, uint16_t diropts)
{
    if(!_started)
        return false;

    Debug_printf("FileSystemSMB::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);

    // skip '/' at beginning
    const char *smb_path = path;
    if (smb_path != nullptr && smb_path[0] == '/')
        smb_path += 1;

    if (strcmp(_last_dir, smb_path) == 0)
    {
        Debug_printf("Use directory cache\n");
    }
    else
    {
        Debug_printf("Fill directory cache\n");

        _dircache.clear();
        // invalidate _last_dir
        _last_dir[0] = '/';
        _last_dir[1] = '\0';

        // Open SMB directory
        struct smb2dir *smb_dir;

        smb_dir = smb2_opendir(_smb, smb_path);
        if (smb_dir == nullptr)
        {
            Debug_printf("Failed to open directory: %s\n", smb2_get_error(_smb));
            return false;
        }

        // Remember last visited directory
        strlcpy(_last_dir, smb_path, MAX_PATHLEN);

        // Populate directory cache with entries
        smb2dirent *smb_de;
        fsdir_entry *fs_de;

        while ((smb_de = smb2_readdir(_smb, smb_dir)) != nullptr)
        {
            // process only files and directories, i.e. skip SMB links - TODO handle links?
            if (smb_de->st.smb2_type != SMB2_TYPE_FILE && smb_de->st.smb2_type != SMB2_TYPE_DIRECTORY)
                continue;

            // skip hidden
            if (smb_de->name[0] == '.')
                continue;

            // new dir entry
            fs_de = &_dircache.new_entry();

            // set entry members
            strlcpy(fs_de->filename, smb_de->name, sizeof(fs_de->filename));
            fs_de->isDir = smb_de->st.smb2_type == SMB2_TYPE_DIRECTORY;
            fs_de->size = (uint32_t)smb_de->st.smb2_size;
            fs_de->modified_time = (time_t)smb_de->st.smb2_mtime;

            if (fs_de->isDir)
            {
                Debug_printf(" add entry: \"%s\"\tDIR\n", fs_de->filename);
            }
            else
            {
                Debug_printf(" add entry: \"%s\"\t%lu\n", fs_de->filename, fs_de->size);
            }
        }
        smb2_closedir(_smb, smb_dir);
    }

    // Apply pattern matching filter and sort entries
    _dircache.apply_filter(pattern, diropts);

    return true;
}

fsdir_entry *FileSystemSMB::dir_read()
{
    return _dircache.read();
}

void FileSystemSMB::dir_close()
{
    // _dircache.clear();
}

uint16_t FileSystemSMB::dir_tell()
{
    return _dircache.tell();
}

bool FileSystemSMB::dir_seek(uint16_t pos)
{
    return _dircache.seek(pos);
}
