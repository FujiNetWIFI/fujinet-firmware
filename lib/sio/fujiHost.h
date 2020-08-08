#ifndef _FUJI_HOST_
#define _FUJI_HOST_

#include <string>

#include "../FileSystem/fnFS.h"

#define MAX_HOSTNAME_LEN 32

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

    void cleanup();
    void unmount();

    int mount_local();
    int mount_tnfs();

public:
    int slotid = -1;
    void set_type(fujiHostType type);

    fujiHostType get_type() { return _type; };

    const char* get_hostname(char *buffer, size_t buffersize);
    const char* get_hostname();

    void set_hostname(const char *hostname);

    bool mount();

    bool exists(const char *path);
    bool exists(const std::string path);

    FILE * open(const char *path, const char *mode = "r");
    FILE * open(const std::string path, const char *mode = "r");

    //FILE * get_filehandle();

    long get_filesize(FILE *filehandle);

    bool dir_open(const char *path);
    fsdir_entry_t * dir_nextfile();
    void dir_close();
    uint16_t dir_tell();
    bool dir_seek(uint16_t position);

    fujiHost() { _type = HOSTTYPE_UNINITIALIZED; };
    ~fujiHost() { set_type(HOSTTYPE_UNINITIALIZED); };
};

#endif // _FUJI_HOST_
