#ifndef FN_FILECACHE_H
#define FN_FILECACHE_H

#include "fnio.h"

#ifndef FNIO_IS_STDIO

#include <string>

typedef struct fc_handle
{
    FileHandler *fh;
    int threshold;
    int max_size;
    int size;
    bool persistent;
    std::string host;
    std::string path;
    std::string name;
} fc_handle;


class FileCache
{
public:

   /**
    * @brief Open existing SD cache file
    * @param host name from host slot
    * @param path file path from device slot
    * @param mode open mode
    * @return pointer to file handler to use or nullptr on error
    */
    static FileHandler *open(const char *host, const char *path, const char *mode);

   /**
    * @brief Create new empty cache file, ready for writes, file is created in memory
    * @param host name from host slot
    * @param path file path from device slot
    * @param threshold size threshold when in memory file is changed to SD file, < 0 to use default threshold, 0 to start on SD
    * @param max_size maximum file size, < 0 for unlimited
    * @return pointer to fc_handle structure or nullptr on error
    */
    static fc_handle *create(const char *host, const char *path, int threshold=-1, int max_size=-1);

   /** 
    * @brief Write data to cache file
    * @return amount of written bytes
    */
    static size_t write(fc_handle *fc, const void *data, size_t len);

   /** 
    * @brief Open cache file (after successful create/write).
    * If cache file is on SD (see threshold), it is flushed/closed first, then opened again.
    * If cache file is still in memory, it is rewound (TODO: mode is ignored, shouldn't be).
    * fc_handle is deleted and cannot be used anymore.
    * @return pointer to file handler to use (can be in memory or SD file) or nullptr on error
    */
    static FileHandler *reopen(fc_handle *fc, const char *mode);

   /** 
    * @brief Remove cache file (after failure)
    * Cache file is deleted from memory or SD.
    * fc_handle is deleted and cannot be used anymore.
    */
   static void remove(fc_handle *fc);
};

#endif //!FNIO_IS_STDIO

#endif // FN_FILECACHE_H
