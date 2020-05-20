#ifndef _TNFSLIB_H
#define _TNFSLIB_H

#include <cstdint>
#include <cstdio>
#include <string>

#include <lwip/netdb.h>

#define TNFS_DEBUG_VERBOSE

#define TNFS_TIMEOUT 6000
#define TNFS_RETRY_DELAY 1000
#define TNFS_RETRIES 5
#define TNFS_DEFAULT_PORT 16384

#define TNFS_CMD_MOUNT 0x00
#define TNFS_CMD_UNMOUNT 0x01

#define TNFS_CMD_OPENDIR 0x10
#define TNFS_CMD_READDIR 0x11
#define TNFS_CMD_CLOSEDIR 0x12
#define TNFS_CMD_MKDIR 0x13
#define TNFS_CMD_RMDIR 0x14

#define TNFS_CMD_READ 0x21
#define TNFS_CMD_WRITE 0x22
#define TNFS_CMD_CLOSE 0x23
#define TNFS_CMD_STAT 0x24
#define TNFS_CMD_LSEEK 0x25
#define TNFS_CMD_UNLINK 0x26
#define TNFS_CMD_CHMOD 0x27
#define TNFS_CMD_RENAME 0x28
#define TNFS_CMD_OPEN 0x29

#define TNFS_CMD_SIZE 0x30
#define TNFS_CMD_FREE 0x31


// https://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html
#define TNFS_OPENMODE_READDONLY 0x0001 //Open read only
#define TNFS_OPENMODE_WRITEONLY 0x0002 //Open write only
#define TNFS_OPENMODE_READWRITE 0x0003   //Open read/write
#define TNFS_OPENMODE_APPEND 0x0008 //Append to the file, if it exists (write only)
#define TNFS_OPENMODE_CREATE 0x0100  //Create the file if it doesn't exist (write only)
#define TNFS_OPENMODE_TRUNCATE 0x0200  //Truncate the file on open for writing
#define TNFS_OPENMODE_EXCLUSIVE 0x0400   //With TNFS_OPENMODE_CREATE, returns an error if the file exists

#define TNFS_CREATEPERM_S_ISUID 04000 //set user ID on execution
#define TNFS_CREATEPERM_S_ISGID 02000 //set group ID on execution
#define TNFS_CREATEPERM_S_ISVTX 01000 //sticky bit
#define TNFS_CREATEPERM_S_IRUSR 00400 //read by owner
#define TNFS_CREATEPERM_S_IWUSR 00200 //write by owner
#define TNFS_CREATEPERM_S_IXUSR 00100 //execute/search by owner
#define TNFS_CREATEPERM_S_IRGRP 00040 //read by group
#define TNFS_CREATEPERM_S_IWGRP 00020 //write by group
#define TNFS_CREATEPERM_S_IXGRP 00010 //execute/search by group
#define TNFS_CREATEPERM_S_IROTH 00004 //read by others
#define TNFS_CREATEPERM_S_IWOTH 00002 //write by others
#define TNFS_CREATEPERM_S_IXOTH 00001 //execute/search by others

#define TNFS_RESULT_SUCCESS 0x00
#define TNFS_RESULT_NOT_PERMITTED 0x01
#define TNFS_RESULT_FILE_NOT_FOUND 0x02
#define TNFS_RESULT_IO_ERROR 0x03
#define TNFS_RESULT_NO_SUCH_DEVICE 0x04
#define TNFS_RESULT_LIST_TOO_LONG 0x05
#define TNFS_RESULT_BAD_FILENUM 0x06
#define TNFS_RESULT_TRY_AGAIN 0x07
#define TNFS_RESULT_OUT_OF_MEMORY 0x08
#define TNFS_RESULT_ACCESS_DENIED 0x09
#define TNFS_RESULT_RESOURCE_BUSY 0x0A
#define TNFS_RESULT_FILE_EXISTS 0x0B
#define TNFS_RESULT_NOT_A_DIRECTORY 0x0C
#define TNFS_RESULT_IS_DIRECTORY 0x0D
#define TNFS_RESULT_INVALID_ARGUMENT 0x0E
#define TNFS_RESULT_FILE_TABLE_OVERFLOW 0x0F
#define TNFS_RESULT_TOO_MANY_FILES_OPEN 0x10
#define TNFS_RESULT_FILE_TOO_LARGE 0x11
#define TNFS_RESULT_NO_SPACE_ON_DEVICE 0x12
#define TNFS_RESULT_CANNOT_SEEK_PIPE 0x13
#define TNFS_RESULT_READONLY_FILESYSTEM 0x14
#define TNFS_RESULT_NAME_TOO_LONG 0x15
#define TNFS_RESULT_FUNCTION_UNIMPLEMENTED 0x16
#define TNFS_RESULT_DIRECTORY_NOT_EMPTY 0x17
#define TNFS_RESULT_TOO_MANY_SYMLINKS 0x18
#define TNFS_RESULT_NO_DATA_AVAILABLE 0x19
#define TNFS_RESULT_OUT_OF_STREAMS 0x1A
#define TNFS_RESULT_PROTOCOL_ERROR 0x1B
#define TNFS_RESULT_BAD_FILE_DESCRIPTOR 0x1C
#define TNFS_RESULT_TOO_MANY_USERS 0x1D
#define TNFS_RESULT_OUT_OF_BUFFER_SPACE 0x1E
#define TNFS_RESULT_ALREADY_IN_PROGRESS 0x1F
#define TNFS_RESULT_STALE_HANDLE 0x20
#define TNFS_RESULT_END_OF_FILE 0x21
#define TNFS_RESULT_INVALID_HANDLE 0xFF

/* Maximum safe IPv4 UDP payload size is generally considered to be 508 bytes
 (packet of 576 bytes - max 60 byte IP header - 8 byte UDP header)
 Additional headers introduced by tunneling, etc., could further reduce this
 size, so a smaller payload is even less likely to be fragmented.

 https://stackoverflow.com/questions/1098897/what-is-the-largest-safe-udp-packet-size-on-the-internet
*/

#define TNFS_HEADER_SIZE 4

union tnfsPacket {
    struct
    {
        uint8_t session_idl;
        uint8_t session_idh;
        uint8_t sequence_num;
        uint8_t command;
        uint8_t payload[504];
    };
    uint8_t rawData[508];
};

#define TNFS_SESSID_SHORT(x) ((uint16_t)x.session_idh << 8 | x.session_idl)

#define TNFS_UINT16_FROM_HILOBYTES(high, low) ((uint16_t)high << 8 | low)

#define TNFS_UINT16_FROM_LOHI_BYTEPTR(bytep) ( (uint16_t)(*(bytep+1)) << 8 | (*(bytep+0)))
#define TNFS_UINT32_FROM_LOHI_BYTEPTR(bytep) ( (uint32_t)(*(bytep+3)) << 24 | (uint32_t)(*(bytep+2)) << 16 | (uint32_t)(*(bytep+1)) << 8 | (*(bytep+0)))

#define TNFS_HIBYTE_FROM_UINT16(value) ((uint8_t)((value >> 8) & 0xFF))
#define TNFS_LOBYTE_FROM_UINT16(value) ((uint8_t)(value & 0xFF))

struct tnfsMountInfo
{
    // These char[] sizes are abitrary...
    char hostname[64];
    in_addr_t host_ip = IPADDR_NONE;
    uint16_t port = TNFS_DEFAULT_PORT;
    char mountpath[64];
    char user[36];
    char password[36];
    uint16_t session; // Stored from server's response to TNFS_MOUNT
    uint16_t min_retry_ms = TNFS_RETRY_DELAY; // Updated from server's response to TNFS_MOUNT
    uint16_t server_version = 0;  // Stored from server's response to TNFS_MOUNT
    uint8_t max_retries = TNFS_RETRIES;
    int timeout_ms = TNFS_TIMEOUT;
    uint8_t current_sequence_num = 0; // Updated with each transaction to the server
    int dir_handle = -1; // Stored from server's response to TNFS_OPENDIR
};

struct tnfsStat
{
    bool isDir;
    uint32_t filesize;
    uint32_t a_time;
    uint32_t m_time;
    uint32_t c_time;
};

class TNFSImpl
{
    //This class implements the physical interface for built-in functions in FS.h
protected:
    // TNFS host parameters
    std::string _host = "";
    uint16_t _port;
    //tnfsSessionID_t _sid;
    std::string _location = "";
    std::string _userid = "";
    std::string _password = "";

public:
    /*
    FileImplPtr open(const char *path, const char *mode) 
    bool exists(const char *path) 
    bool rename(const char *pathFrom, const char *pathTo) 
    bool remove(const char *path) 
    bool mkdir(const char *path) 
    bool rmdir(const char *path) 
    std::string host();
    uint16_t port();
    tnfsSessionID_t sid();
    std::string location();
    std::string userid();
    std::string password();
    */
};

class TNFSFileImpl
{
    //This class implements the physical interface for built-in functions in the File class defined in FS.h

protected:
    TNFSImpl *fs;
    int fid;
    char fn[256];
    //tnfsStat_t stats;

public:
    size_t write(const uint8_t *buf, size_t size);
    size_t read(uint8_t *buf, size_t size);
    void flush();
    //bool seek(uint32_t pos, SeekMode mode);
    size_t position() const;
    size_t size() const;
    void close();
    const char *name() const;
    time_t getLastWrite();
    bool isDirectory(void);
    struct dirent openNextFile(const char *mode);
    void rewindDirectory(void);
    operator bool();
};

bool tnfs_mount(tnfsMountInfo &m_info);
bool tnfs_umount(tnfsMountInfo &m_info);

bool tnfs_opendir(tnfsMountInfo &m_info, const char *directory);
bool tnfs_readdir(tnfsMountInfo &m_info, char *dir_entry, int dir_entry_len);
bool tnfs_closedir(tnfsMountInfo &m_info);

int tnfs_rmdir(tnfsMountInfo &m_info, const char *directory);
int tnfs_mkdir(tnfsMountInfo &m_info, const char *directory);

int tnfs_stat(tnfsMountInfo &m_info, tnfsStat &filestat, const char *filepath);


/*
int tnfs_open(TNFSImpl *F, const char *mountPath, uint8_tflag_lsb, uint8_tflag_msb);
*/
bool tnfs_close(TNFSImpl *F, int fid);
size_t tnfs_write(TNFSImpl *F, int fid, const uint8_t *buf, unsigned short len);
size_t tnfs_read(TNFSImpl *F, int fid, uint8_t *buf, unsigned short size);
bool tnfs_seek(TNFSImpl *F, int fid, long offset);

#endif //_TNFSLIB_H
