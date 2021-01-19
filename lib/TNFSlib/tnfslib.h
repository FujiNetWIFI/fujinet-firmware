#ifndef _TNFSLIB_H
#define _TNFSLIB_H

#include <cstdint>
#include <cstdio>
#include <string>

#include <lwip/netdb.h>

#include "tnfslibMountInfo.h"

#define TNFS_CMD_MOUNT 0x00
#define TNFS_CMD_UNMOUNT 0x01

#define TNFS_CMD_OPENDIR 0x10
#define TNFS_CMD_READDIR 0x11
#define TNFS_CMD_CLOSEDIR 0x12
#define TNFS_CMD_MKDIR 0x13
#define TNFS_CMD_RMDIR 0x14
#define TNFS_CMD_TELLDIR 0x15
#define TNFS_CMD_SEEKDIR 0x16
#define TNFS_CMD_OPENDIRX 0x17
#define TNFS_CMD_READDIRX 0x18

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
#define TNFS_OPENMODE_READ 0x0001             // Open read only
#define TNFS_OPENMODE_WRITE 0x0002            // Open write only
#define TNFS_OPENMODE_READWRITE 0x0003        // Open read/write
#define TNFS_OPENMODE_WRITE_APPEND 0x0008     // Append to the file if it exists (write only)
#define TNFS_OPENMODE_WRITE_CREATE 0x0100     // Create the file if it doesn't exist (write only)
#define TNFS_OPENMODE_WRITE_TRUNCATE 0x0200   // Truncate the file on open for writing
#define TNFS_OPENMODE_CREATE_EXCLUSIVE 0x0400 // With TNFS_OPENMODE_CREATE, returns an error if the file exists

#define TNFS_CREATEPERM_S_ISUID 04000 // Set user ID on execution
#define TNFS_CREATEPERM_S_ISGID 02000 // Set group ID on execution
#define TNFS_CREATEPERM_S_ISVTX 01000 // Sticky bit
#define TNFS_CREATEPERM_S_IRUSR 00400 // Read by owner
#define TNFS_CREATEPERM_S_IWUSR 00200 // Write by owner
#define TNFS_CREATEPERM_S_IXUSR 00100 // Execute/search by owner
#define TNFS_CREATEPERM_S_IRGRP 00040 // Read by group
#define TNFS_CREATEPERM_S_IWGRP 00020 // Write by group
#define TNFS_CREATEPERM_S_IXGRP 00010 // Execute/search by group
#define TNFS_CREATEPERM_S_IROTH 00004 // Read by others
#define TNFS_CREATEPERM_S_IWOTH 00002 // Write by others
#define TNFS_CREATEPERM_S_IXOTH 00001 // Execute/search by others

#define TNFS_READDIRX_DIR 0x01 // Entry flag returned in tnfs_reddirx
#define TNFS_READDIRX_HIDDEN 0x02 // Entry flag returned in tnfs_reddirx
#define TNFS_READDIRX_SPECIAL 0x04 // Entry flag returned in tnfs_reddirx

#define TNFS_READDIRX_STATUS_EOF 0x01 // Status flag returned by tnfs_readdirx

#define TNFS_DIROPT_NO_FOLDERSFIRST 0x01 // Don't return folders before files
#define TNFS_DIROPT_NO_SKIPHIDDEN 0x02   // Don't skip hidden files
#define TNFS_DIROPT_NO_SKIPSPECIAL 0x04  // Don't skip special files
#define TNFS_DIROPT_DIR_PATTERN 0x08     // Apply wildcard pattern to directories, too

#define TNFS_DIRSORT_NONE 0x01       // Do not perform any sorting
#define TNFS_DIRSORT_CASE 0x02       // Perform case-sensitve sort
#define TNFS_DIRSORT_DESCENDING 0x04 // Sort in descending order
#define TNFS_DIRSORT_MODIFIED 0x08   // Sort by modified time, not name
#define TNFS_DIRSORT_SIZE 0x10       // Sort by size, not name

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

// 7/18/2020 - Updated to 532, since that's the actual size packet TNFSD can send with its current build settings
// 532 gives us (68 + 4 + 532) 604-byte packets - bigger than recommended, but allows for the
// 532-byte IO payloads TNFSD is built with by default; allows for 4 128-byte sectors to be transmitted
#define TNFS_PAYLOAD_SIZE 532

// Maximum size of buffer during tnfs_read() and tnfs_write()
#define TNFS_MAX_READWRITE_PAYLOAD (TNFS_PAYLOAD_SIZE - 3) // 1 byte is needed for FD and 2 for size

union tnfsPacket {
    struct
    {
        uint8_t session_idl;
        uint8_t session_idh;
        uint8_t sequence_num;
        uint8_t command;
        uint8_t payload[TNFS_PAYLOAD_SIZE];
    };
    uint8_t rawData[TNFS_HEADER_SIZE + TNFS_PAYLOAD_SIZE];
};

struct tnfsStat
{
    bool isDir;
    uint32_t filesize;
    uint32_t a_time;
    uint32_t m_time;
    uint32_t c_time;
    uint16_t mode;
};

// Retruns a uint16 value given two bytes in high-low order
#define TNFS_UINT16_FROM_HILOBYTES(high, low) ((uint16_t)high << 8 | low)

// Returns a uint16 value from a pointer to two bytes in little-ending order
#define TNFS_UINT16_FROM_LOHI_BYTEPTR(bytep) ((uint16_t)(*(bytep + 1)) << 8 | (*(bytep + 0)))
// Returns a uint32 value from a pointer to four bytes in little-ending order
#define TNFS_UINT32_FROM_LOHI_BYTEPTR(bytep) ((uint32_t)(*(bytep + 3)) << 24 | (uint32_t)(*(bytep + 2)) << 16 | (uint32_t)(*(bytep + 1)) << 8 | (*(bytep + 0)))

// Takes UINT32 value and pushes it into 4 consecutive bytes in little-endian order
#define TNFS_UINT32_TO_LOHI_BYTEPTR(value, bytep) \
    {                                             \
        (bytep)[0] = value & 0xFFUL;              \
        (bytep)[1] = value >> 8 & 0xFFUL;         \
        (bytep)[2] = value >> 16 & 0xFFUL;        \
        (bytep)[3] = value >> 24 & 0xFFUL;        \
    }

// Returns the high byte (MSB) of a uint16 value
#define TNFS_HIBYTE_FROM_UINT16(value) ((uint8_t)((value >> 8) & 0xFF))
// Returns the low byte (LSB) of a uint16 value
#define TNFS_LOBYTE_FROM_UINT16(value) ((uint8_t)(value & 0xFF))

// Checks that value is >= 0 and <= 255
#define TNFS_VALID_AS_UINT8(value) (value >= 0 && value <= 255)

int tnfs_mount(tnfsMountInfo *m_info);
int tnfs_umount(tnfsMountInfo *m_info);

//int tnfs_opendir(tnfsMountInfo *m_info, const char *directory);
int tnfs_opendirx(tnfsMountInfo *m_info, const char *directory, uint8_t sortopts = 0, uint8_t diropts = 0, const char *pattern = nullptr, uint16_t maxresults = 0);
//int tnfs_readdir(tnfsMountInfo *m_info, char *dir_entry, int dir_entry_len);
int tnfs_readdirx(tnfsMountInfo *m_info, tnfsStat *filestat, char *dir_entry, int dir_entry_len);
int tnfs_closedir(tnfsMountInfo *m_info);

int tnfs_telldir(tnfsMountInfo *m_info, uint16_t *position);
int tnfs_seekdir(tnfsMountInfo *m_info, uint16_t position);

int tnfs_rmdir(tnfsMountInfo *m_info, const char *directory);
int tnfs_mkdir(tnfsMountInfo *m_info, const char *directory);

int tnfs_size(tnfsMountInfo *m_info, uint32_t *size);
int tnfs_free(tnfsMountInfo *m_info, uint32_t *size);

int tnfs_open(tnfsMountInfo *m_info, const char *filepath, uint16_t open_mode, uint16_t create_perms, int16_t *file_handle);
int tnfs_read(tnfsMountInfo *m_info, int16_t file_handle, uint8_t *buffer, uint16_t bufflen, uint16_t *resultlen);
int tnfs_write(tnfsMountInfo *m_info, int16_t file_handle, uint8_t *buffer, uint16_t bufflen, uint16_t *resultlen);
int tnfs_close(tnfsMountInfo *m_info, int16_t file_handle);
int tnfs_stat(tnfsMountInfo *m_info, tnfsStat *filestat, const char *filepath);
int tnfs_lseek(tnfsMountInfo *m_info, int16_t file_handle, int32_t position, uint8_t type, uint32_t *new_position = nullptr, bool skip_cache = false);
int tnfs_unlink(tnfsMountInfo *m_info, const char *filepath);
int tnfs_chmod(tnfsMountInfo *m_info, const char *filepath, uint16_t mode);
int tnfs_rename(tnfsMountInfo *m_info, const char *old_filepath, const char *new_filepath);

int tnfs_chdir(tnfsMountInfo *m_info, const char *dirpath);
const char *tnfs_getcwd(tnfsMountInfo *m_info);
const char *tnfs_filepath(tnfsMountInfo *m_info, int16_t file_handle);

int tnfs_code_to_errno(int tnfs_code);

#endif //_TNFSLIB_H
