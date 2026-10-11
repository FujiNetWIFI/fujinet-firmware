
#include "tnfslib.h"

#include <cstdlib>
#include <sys/stat.h>
#include <errno.h>
#include <mutex>
#include "compat_string.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "bus.h"
#include "fnUDP.h"
#include "fnTcpClient.h"
#include "tnfslib_udp.h"

#include "utils.h"

#include "tnfslib_internal.h"

// ESTALE, ENOSTR and ENODATA not in errno.h on Windows/MinGW
#ifndef ESTALE
#define ESTALE 116
#endif
#ifndef ENOSTR
#define ENOSTR 60
#endif
#ifndef ENODATA
#define ENODATA 61
#endif

using namespace std;
// ------------------------------------------------
// DEBUG STUFF FROM HERE DOWN
// ------------------------------------------------
/*
  Dump TNFS packet to debug
  unsigned short len - packet data payload length
  bool isResponse - parse result code
*/
void _tnfs_debug_packet(const tnfsPacket &pkt, unsigned short payload_size, bool isResponse)
{
#ifdef VERBOSE_TNFS
    // Remove header bytes from count of response packets since we only care about the count of the data payload
    if (isResponse)
    {
        payload_size -= TNFS_HEADER_SIZE;
        Debug_printf("TNFS << RX cmd: %s, len: %d, response (%hhu): %s\r\n", _tnfs_command_string(pkt.command), payload_size, pkt.payload[0], _tnfs_result_code_string(pkt.payload[0]));
    }
    else
        Debug_printf("TNFS >> TX cmd: %s, len: %d\r\n", _tnfs_command_string(pkt.command), payload_size);

    Debug_printf("\t[%02x%02x %02x %02x] ", pkt.session_idh, pkt.session_idl, pkt.sequence_num, pkt.command);
    for (int i = 0; i < payload_size; i++)
        Debug_printf("%02x ", pkt.payload[i]);
    Debug_println("\r\n");
#endif
}

const char *_tnfs_command_string(int command)
{
#ifdef VERBOSE_TNFS
    switch (command)
    {
    case TNFS_CMD_MOUNT:
        return "MOUNT";
    case TNFS_CMD_UNMOUNT:
        return "UNMOUNT";
    case TNFS_CMD_OPENDIR:
        return "OPENDIR";
    case TNFS_CMD_READDIR:
        return "READDIR";
    case TNFS_CMD_CLOSEDIR:
        return "CLOSEDIR";
    case TNFS_CMD_MKDIR:
        return "MKDIR";
    case TNFS_CMD_RMDIR:
        return "RMDIR";
    case TNFS_CMD_READ:
        return "READ";
    case TNFS_CMD_WRITE:
        return "WRITE";
    case TNFS_CMD_CLOSE:
        return "CLOSE";
    case TNFS_CMD_STAT:
        return "STAT";
    case TNFS_CMD_LSEEK:
        return "LSEEK";
    case TNFS_CMD_UNLINK:
        return "UNLINK";
    case TNFS_CMD_CHMOD:
        return "CHMOD";
    case TNFS_CMD_RENAME:
        return "RENAME";
    case TNFS_CMD_OPEN:
        return "OPEN";
    case TNFS_CMD_SIZE:
        return "SIZE";
    case TNFS_CMD_FREE:
        return "FREE";
    case TNFS_CMD_TELLDIR:
        return "TELLDIR";
    case TNFS_CMD_SEEKDIR:
        return "SEEKDIR";
    case TNFS_CMD_OPENDIRX:
        return "OPENDIRX";
    case TNFS_CMD_READDIRX:
        return "READDIRX";
    default:
        return "?";
    }
#else
    return nullptr;
#endif
}

const char *_tnfs_result_code_string(int resultcode)
{
#ifdef VERBOSE_TNFS
    switch (resultcode)
    {
    case TNFS_RESULT_SUCCESS:
        return "Success";
    case TNFS_RESULT_NOT_PERMITTED:
        return "Operation not permitted";
    case TNFS_RESULT_FILE_NOT_FOUND:
        return "No such file or directory";
    case TNFS_RESULT_IO_ERROR:
        return "I/O error";
    case TNFS_RESULT_NO_SUCH_DEVICE:
        return "No such device or address";
    case TNFS_RESULT_LIST_TOO_LONG:
        return "Argument list too long";
    case TNFS_RESULT_BAD_FILENUM:
        return "Bad file number";
    case TNFS_RESULT_TRY_AGAIN:
        return "Try again";
    case TNFS_RESULT_OUT_OF_MEMORY:
        return "Out of memory";
    case TNFS_RESULT_ACCESS_DENIED:
        return "Permission denied";
    case TNFS_RESULT_RESOURCE_BUSY:
        return "Device or resource busy";
    case TNFS_RESULT_FILE_EXISTS:
        return "File exists";
    case TNFS_RESULT_NOT_A_DIRECTORY:
        return "Is not a directory";
    case TNFS_RESULT_IS_DIRECTORY:
        return "Is a directory";
    case TNFS_RESULT_INVALID_ARGUMENT:
        return "Invalid argument";
    case TNFS_RESULT_FILE_TABLE_OVERFLOW:
        return "File table overflow";
    case TNFS_RESULT_TOO_MANY_FILES_OPEN:
        return "Too many open files";
    case TNFS_RESULT_FILE_TOO_LARGE:
        return "File too large";
    case TNFS_RESULT_NO_SPACE_ON_DEVICE:
        return "No space left on device";
    case TNFS_RESULT_CANNOT_SEEK_PIPE:
        return "Attempt to seek on a FIFO or pipe";
    case TNFS_RESULT_READONLY_FILESYSTEM:
        return "Read only filesystem";
    case TNFS_RESULT_NAME_TOO_LONG:
        return "Filename too long";
    case TNFS_RESULT_FUNCTION_UNIMPLEMENTED:
        return "Function not implemented";
    case TNFS_RESULT_DIRECTORY_NOT_EMPTY:
        return "Directory not empty";
    case TNFS_RESULT_TOO_MANY_SYMLINKS:
        return "Too many symbolic links";
    case TNFS_RESULT_NO_DATA_AVAILABLE:
        return "No data available";
    case TNFS_RESULT_OUT_OF_STREAMS:
        return "Out of streams resources";
    case TNFS_RESULT_FUJI_ERROR:
        return "Protocol error";
    case TNFS_RESULT_BAD_FILE_DESCRIPTOR:
        return "File descriptor in bad state";
    case TNFS_RESULT_TOO_MANY_USERS:
        return "Too many users";
    case TNFS_RESULT_OUT_OF_BUFFER_SPACE:
        return "No buffer space avaialable";
    case TNFS_RESULT_ALREADY_IN_PROGRESS:
        return "Operation already in progress";
    case TNFS_RESULT_STALE_HANDLE:
        return "Stale TNFS handle";
    case TNFS_RESULT_END_OF_FILE:
        return "End of file";
    case TNFS_RESULT_INVALID_HANDLE:
        return "Invalid TNFS handle";
    default:
        return "Unknown result code";
    }
#else
    return nullptr;
#endif
}

int tnfs_code_to_errno(int tnfs_code)
{
    switch (tnfs_code)
    {
    case TNFS_RESULT_SUCCESS:
        return 0;
    case TNFS_RESULT_NOT_PERMITTED:
        return EPERM;
    case TNFS_RESULT_FILE_NOT_FOUND:
        return ENOENT;
    case TNFS_RESULT_IO_ERROR:
        return EIO;
    case TNFS_RESULT_NO_SUCH_DEVICE:
        return ENXIO;
    case TNFS_RESULT_LIST_TOO_LONG:
        return E2BIG;
    case TNFS_RESULT_BAD_FILENUM:
        return EBADF;
    case TNFS_RESULT_TRY_AGAIN:
        return EAGAIN;
    case TNFS_RESULT_OUT_OF_MEMORY:
        return ENOMEM;
    case TNFS_RESULT_ACCESS_DENIED:
        return EACCES;
    case TNFS_RESULT_RESOURCE_BUSY:
        return EBUSY;
    case TNFS_RESULT_FILE_EXISTS:
        return EEXIST;
    case TNFS_RESULT_NOT_A_DIRECTORY:
        return ENOTDIR;
    case TNFS_RESULT_IS_DIRECTORY:
        return EISDIR;
    case TNFS_RESULT_INVALID_ARGUMENT:
        return EINVAL;
    case TNFS_RESULT_FILE_TABLE_OVERFLOW:
        return ENFILE;
    case TNFS_RESULT_TOO_MANY_FILES_OPEN:
        return EMFILE;
    case TNFS_RESULT_FILE_TOO_LARGE:
        return EFBIG;
    case TNFS_RESULT_NO_SPACE_ON_DEVICE:
        return ENOSPC;
    case TNFS_RESULT_CANNOT_SEEK_PIPE:
        return ESPIPE;
    case TNFS_RESULT_READONLY_FILESYSTEM:
        return EROFS;
    case TNFS_RESULT_NAME_TOO_LONG:
        return ENAMETOOLONG;
    case TNFS_RESULT_FUNCTION_UNIMPLEMENTED:
        return ENOSYS;
    case TNFS_RESULT_DIRECTORY_NOT_EMPTY:
        return ENOTEMPTY;
    case TNFS_RESULT_TOO_MANY_SYMLINKS:
        return ELOOP;
    case TNFS_RESULT_NO_DATA_AVAILABLE:
        return ENODATA;
    case TNFS_RESULT_OUT_OF_STREAMS:
        return ENOSTR;
    case TNFS_RESULT_FUJI_ERROR:
        return EPROTO;
    case TNFS_RESULT_BAD_FILE_DESCRIPTOR:
        return EBADF; // Different from EBADFD documented
    case TNFS_RESULT_TOO_MANY_USERS:
        return 0xFF;
    case TNFS_RESULT_OUT_OF_BUFFER_SPACE:
        return ENOBUFS;
    case TNFS_RESULT_ALREADY_IN_PROGRESS:
        return EALREADY;
    case TNFS_RESULT_STALE_HANDLE:
        return ESTALE;
    case TNFS_RESULT_END_OF_FILE:
        return EOF;
    case TNFS_RESULT_INVALID_HANDLE:
        return 0xFF;
    case -1:
        return ENETRESET; // Generic "network error"
    default:
        return 0xFF;
    }
}
