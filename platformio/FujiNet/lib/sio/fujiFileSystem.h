#ifndef FUJIFILESYSTEM_H
#define FUJIFILESYSTEM_H

#include "../FileSystem/fnFS.h"

#define MAX_HOSTNAME_LEN 32

enum fujiFSType
{
    FNFILESYS_UNINITIALIZED = 0,
    FNFILESYS_LOCAL,
    FNFILESYS_TNFS
};

class fujiFileSystem
{
private:
    const char * _sdhostname = "SD";
    FileSystem *_fs = nullptr;
    fujiFSType _type;
    char _hostname[MAX_HOSTNAME_LEN];

    void cleanup();
    void unmount();

    int mount_local(const char *devicename);
    int mount_tnfs(const char *hostname);

public:
    int slotid = -1;
    void set_type(fujiFSType type);

    fujiFSType get_type() { return _type; };

    const char* get_hostname(char *buffer, size_t buffersize);
    const char* get_hostname();

    bool mount(const char *devicename);

    bool exists(const char *path);
    bool exists(const String &path);

    FILE * open(const char *path, const char *mode = "r");
    FILE * open(const String &path, const char *mode = "r");

    bool dir_open(const char *path);
    fsdir_entry_t * dir_nextfile();
    void dir_close();

    fujiFileSystem() { _type = FNFILESYS_UNINITIALIZED; };
    ~fujiFileSystem() { set_type(FNFILESYS_UNINITIALIZED); };
};

#endif // FUJIFILESYSTEM_H
