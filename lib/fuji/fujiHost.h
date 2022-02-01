#ifndef _FUJI_HOST_
#define _FUJI_HOST_

#include "fnFS.h"

#define MAX_HOSTNAME_LEN 32
#define MAX_HOST_PREFIX_LEN 256

enum fujiHostType
{
    HOSTTYPE_UNINITIALIZED = 0,
    HOSTTYPE_LOCAL,
    HOSTTYPE_TNFS
};

class fujiHost
{
private:
    const char * _sdhostname = "SD";
    FileSystem *_fs = nullptr;
    fujiHostType _type;
    char _hostname[MAX_HOSTNAME_LEN] = { '\0' };
    char _prefix[MAX_HOST_PREFIX_LEN] = { '\0' };

    void cleanup();
    void unmount();

    int mount_local();
    int mount_tnfs();

public:
    int slotid = -1;

    fujiHost() { _type = HOSTTYPE_UNINITIALIZED; };
    ~fujiHost() { set_type(HOSTTYPE_UNINITIALIZED); };

    void set_type(fujiHostType type);
    fujiHostType get_type() { return _type; };

    void set_hostname(const char *hostname);
    const char* get_hostname(char *buffer, size_t buffersize);
    const char* get_hostname();

    bool mount();

    // Host prefixes are used for host file operations that take a path (file_exists, file_open, dir_open)
    void set_prefix(const char *prefix);
    const char* get_prefix(char *buffer, size_t buffersize);
    const char* get_prefix();

    // File functions
    bool file_exists(const char *path);
    FILE * file_open(const char *path, char *fullpath, int fullpathlen, const char *mode);
    long file_size(FILE *filehandle);

    // Directory functions
    bool dir_open(const char *path, const char *pattern, uint16_t options = 0);
    void dir_close();
    fsdir_entry_t * dir_nextfile();
    uint16_t dir_tell();
    bool dir_seek(uint16_t position);

};

#endif // _FUJI_HOST_
