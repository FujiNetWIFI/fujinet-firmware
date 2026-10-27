#ifndef FN_FSDIRENTRY_H
#define FN_FSDIRENTRY_H

#include <cstdint>
#include <ctime>

#ifdef ESP_PLATFORM
#define MAX_PATHLEN 256
#else
#define MAX_PATHLEN 1024
#endif

#define FNFS_INVALID_DIRPOS 0xFFFF

enum dir_option_t : uint16_t
{
    DIR_OPTION_UNSORTED   = 0x0001, // Don't sort
    DIR_OPTION_DESCENDING = 0x0002, // Sort descending, not ascending
    DIR_OPTION_FILEDATE   = 0x0004, // Sort by date, not name
};


struct fsdir_entry
{
    char filename[MAX_PATHLEN];
    bool isDir;
    uint32_t size;
    time_t modified_time;
};
typedef struct fsdir_entry fsdir_entry_t;

#endif // FN_FSDIRENTRY_H
