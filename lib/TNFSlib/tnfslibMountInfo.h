#ifndef _TNFSLIB_MOUNTINFO_H
#define _TNFSLIB_MOUNTINFO_H

#include <lwip/netdb.h>


#define TNFS_DEFAULT_PORT 16384
#define TNFS_RETRIES 10 // Number of times to retry if we fail to send/receive a packet
#define TNFS_TIMEOUT 2000 // This is how long we wait for a reply packet from the server before trying again
#define TNFS_RETRY_DELAY 1000 // Default delay before retrying. Server will provide a minimum during TNFS_CMD_MOUNT
#define TNFS_MAX_BACKOFF_DELAY 3000 // Longest we'll wait if server sends us a EAGAIN error
#define TNFS_MAX_FILE_HANDLES 8 // Max number of file handles we'll open to the server
#define TNFS_MAX_FILELEN 256

#define TNFS_FILE_CACHE_SIZE 512 // 4 * 128 fits in a single packet when TNFS_MAX_READWRITE_PAYLOAD is 512

#define TNFS_INVALID_HANDLE -1
#define TNFS_INVALID_SESSION 0 // We're assuming a '0' is never a valid session ID

#define TNFS_MAX_DIRCACHE_ENTRIES 32 // Max number of directory cache entries we'll store

// Some things we need to keep track of for every file we open
struct tnfsFileHandleInfo
{
    uint8_t handle_id = 0;

    uint32_t file_position = 0; // Current actual file position
    uint32_t file_size = 0;
    uint32_t cached_pos = 0; // File position the client thinks we're at (usually somewhere in the cached region)
    uint32_t cache_start = 0; // The file position at which the cache starts
    uint32_t cache_available = 0; // Number of valid bytes in the cache

    bool cache_modified = false; // Notes if we've written to the cache

    uint8_t cache[TNFS_FILE_CACHE_SIZE];
    char filename[TNFS_MAX_FILELEN];
};

// A place to store each directory entry we cache from a response to TNFS_READDIRX
struct tnfsDirCacheEntry
{
    uint16_t dirpos;
    uint8_t flags;
    uint32_t filesize;
    uint32_t m_time;
    uint32_t c_time;
    char entryname[TNFS_MAX_FILELEN];
};

// Everything we need to know about and keep track of for the server we're talking to
class tnfsMountInfo
{
private:
    tnfsFileHandleInfo * _file_handles[TNFS_MAX_FILE_HANDLES] = { nullptr }; // Stored from server's responses to TNFS_OPEN
    tnfsDirCacheEntry * _dir_cache[TNFS_MAX_DIRCACHE_ENTRIES] = { nullptr };
    uint16_t _dir_cache_current = 0;
    uint16_t _dir_cache_count = 0;
    bool _dir_cache_eof = false;

public:
    ~tnfsMountInfo();

    tnfsMountInfo(){};
    tnfsMountInfo(const char *host_name, uint16_t host_port = TNFS_DEFAULT_PORT);
    tnfsMountInfo(in_addr_t host_address, uint16_t host_port = TNFS_DEFAULT_PORT);

    tnfsFileHandleInfo * new_filehandleinfo();
    tnfsFileHandleInfo * get_filehandleinfo(uint8_t filehandle);
    void delete_filehandleinfo(uint8_t filehandle);
    void delete_filehandleinfo(tnfsFileHandleInfo * pFilehandle);

    tnfsDirCacheEntry * new_dircache_entry();
    tnfsDirCacheEntry * next_dircache_entry();

    int tell_dircache_entry();
    void empty_dircache();
    uint16_t count_dircache() { return _dir_cache_count; };
    void set_dircache_eof() { _dir_cache_eof = true; };
    bool get_dircache_eof() { return _dir_cache_eof; };

    // These char[] sizes are abitrary...
    char hostname[64] = { '\0' };
    in_addr_t host_ip = IPADDR_NONE;
    uint16_t port = TNFS_DEFAULT_PORT;
    char mountpath[64] = { '\0' };
    char user[12] = { '\0' };
    char password[12] = { '\0' };
    char current_working_directory[TNFS_MAX_FILELEN] = { '\0' };
    uint16_t session = TNFS_INVALID_SESSION; // Stored from server's response to TNFS_MOUNT
    uint16_t min_retry_ms = TNFS_RETRY_DELAY; // Updated from server's response to TNFS_MOUNT
    uint16_t server_version = 0;  // Stored from server's response to TNFS_MOUNT
    uint8_t max_retries = TNFS_RETRIES;
    int timeout_ms = TNFS_TIMEOUT;
    uint8_t current_sequence_num = 0; // Updated with each transaction to the server

    int16_t dir_handle = TNFS_INVALID_HANDLE; // Stored from server's response to TNFS_OPENDIR
    uint16_t dir_entries = 0; // Stored from server's response to TNFS_OPENDIRX
};

#endif // _TNFSLIB_MOUNTINFO_H
