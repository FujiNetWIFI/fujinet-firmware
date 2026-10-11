
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

using namespace std;
/*
    Opens directory and stores directory handle in tnfsMountInfo.dir_handle
    sortopts = zero or more TNFS_DIRSORT flags
    diropts = zero or more TNFS_DIROPT flags
    pattern = zero-terminated wildcard pattern string
    maxresults = max number of results to return or zero for unlimited
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_opendirx(tnfsMountInfo *m_info, const char *directory, uint8_t sortopts, uint8_t diropts, const char *pattern, uint16_t maxresults)
{
    if (m_info == nullptr || directory == nullptr)
        return -1;

#define OFFSET_OPENDIRX_DIROPT 0
#define OFFSET_OPENDIRX_SORTOPT 1
#define OFFSET_OPENDIRX_MAXRESULTS 2
#define OFFSET_OPENDIRX_PATTERN 4

// Number of bytes before the two null-terminated strings start
#define OPENDIRX_HEADERBYTES 4

    // Throw out any existing cached directory entries
    m_info->empty_dircache();

    tnfsPacket packet;
    packet.command = TNFS_CMD_OPENDIRX;

    packet.payload[OFFSET_OPENDIRX_DIROPT] = diropts;
    packet.payload[OFFSET_OPENDIRX_SORTOPT] = sortopts;

    packet.payload[OFFSET_OPENDIRX_MAXRESULTS] = TNFS_LOBYTE_FROM_UINT16(maxresults);
    packet.payload[OFFSET_OPENDIRX_MAXRESULTS + 1] = TNFS_HIBYTE_FROM_UINT16(maxresults);

    // Copy the pattern or an empty string
    strlcpy((char *)(packet.payload + OFFSET_OPENDIRX_PATTERN),
        pattern == nullptr ? "" : pattern,
        sizeof(packet.payload) - OPENDIRX_HEADERBYTES - 1);

    // Calculate the new offset to the path taking the pattern string into account
    int pathoffset = strlen((char *)(packet.payload + OFFSET_OPENDIRX_PATTERN)) + OPENDIRX_HEADERBYTES + 1;

    // Copy the directory into the right spot in the packet and get its string len
    int pathlen = _tnfs_adjust_with_full_path(m_info,
        (char *)(packet.payload + pathoffset), directory, sizeof(packet.payload) - pathoffset);

#ifdef VERBOSE_TNFS
    Debug_printf("TNFS open directory: sortopts=0x%02x diropts=0x%02x maxresults=0x%04x pattern=\"%s\" path=\"%s\"\r\n",
      sortopts, diropts, maxresults, (char *)(packet.payload + OFFSET_OPENDIRX_PATTERN), (char *)(packet.payload + pathoffset));
#endif

    if (_tnfs_transaction(m_info, packet, pathoffset + pathlen + 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            m_info->dir_handle = packet.payload[1];
            m_info->dir_entries = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + 2);
            Debug_printf("Directory opened, handle ID: %hd, entries: %u\r\n", m_info->dir_handle, m_info->dir_entries);
        }
        return packet.payload[0];
    }
    return -1;
}

void _readdirx_fill_response(tnfsDirCacheEntry *pCached, tnfsStat *filestat, char *dir_entry, int dir_entry_len)
{
    filestat->isDir = pCached->flags & TNFS_READDIRX_DIR ? true : false;
    filestat->filesize = pCached->filesize;
    filestat->m_time = pCached->m_time;
    filestat->c_time = pCached->c_time;
    filestat->a_time = 0;

    strlcpy(dir_entry, pCached->entryname, dir_entry_len);

#ifdef VERBOSE_TNFS
    {
        char t_m[80];
        char t_c[80];
        const char *tfmt ="%Y-%m-%d %H:%M:%S";
        time_t tt = filestat->m_time;
        strftime(t_m, sizeof(t_m), tfmt, localtime(&tt));
        tt = filestat->c_time;
        strftime(t_c, sizeof(t_c), tfmt, localtime(&tt));
        Debug_printf("\t_readdirx_fill_response: dir: %s, size: %lu, mtime: %s, ctime: %s \"%s\"\r\n",
            filestat->isDir ? "Yes" : "no",
            filestat->filesize, t_m, t_c, dir_entry );
    }
#endif
}

/*
    Reads next available file using open directory handle specified in
    tnfsMountInfo.dir_handle
    dir_entry filled with filename up to dir_entry_len
 returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
*/
int tnfs_readdirx(tnfsMountInfo *m_info, tnfsStat *filestat, char *dir_entry, int dir_entry_len)
{
    // Check for a valid open handle ID
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    // See if we have an entry in our directory cache to return first
    tnfsDirCacheEntry *pCached = m_info->next_dircache_entry();
    if(pCached != nullptr)
    {
#ifdef VERBOSE_TNFS
        Debug_print("tnfs_readdirx responding from cached entry\r\n");
#endif
        _readdirx_fill_response(pCached, filestat, dir_entry, dir_entry_len);
        return 0;
    }

    // If the cache was empty and the EOF flag was set, just respond with an EOF error
    if(m_info->get_dircache_eof() == true)
    {
#ifdef VERBOSE_TNFS
        Debug_print("tnfs_readdirx returning EOF based on cached value\r\n");
#endif
        return TNFS_RESULT_END_OF_FILE;
    }

    // Invalidate the cache before loading more
    m_info->empty_dircache();

#define OFFSET_READDIRX_FLAGS 0
#define OFFSET_READDIRX_SIZE 1
#define OFFSET_READDIRX_MTIME 5
#define OFFSET_READDIRX_CTIME 9
#define OFFSET_READDIRX_PATH 13

    tnfsPacket packet;
    packet.command = TNFS_CMD_READDIRX;
    packet.payload[0] = m_info->dir_handle;
    // Number of responses to read
    packet.payload[1] = TNFS_MAX_DIRCACHE_ENTRIES;

    if (_tnfs_transaction(m_info, packet, 2))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            uint8_t response_count = packet.payload[1];
            uint8_t response_status = packet.payload[2];
            uint16_t dirpos = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + 3);

            // Set our EOF flag if the server tells us there's no more after this
            if(response_status & TNFS_READDIRX_STATUS_EOF)
                m_info->set_dircache_eof();

#ifdef VERBOSE_TNFS
            Debug_printf("tnfs_readdirx resp_count=%hu, dirpos=%hu, status=%hu\r\n", response_count, dirpos, response_status);
#endif

            // Fill our directory cache using the returned values
            int current_offset = 5;
            for(int i = 0; i < response_count; i++)
            {
                tnfsDirCacheEntry *pEntry = m_info->new_dircache_entry();
                if(pEntry != nullptr)
                {
                    pEntry->dirpos = dirpos + i;
                    pEntry->flags =
                        packet.payload[current_offset + OFFSET_READDIRX_FLAGS];
                    pEntry->filesize =
                        TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + current_offset + OFFSET_READDIRX_SIZE);
                    pEntry->m_time =
                        TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + current_offset + OFFSET_READDIRX_MTIME);
                    pEntry->c_time =
                        TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + current_offset + OFFSET_READDIRX_CTIME);

                    int name_len = strlcpy(pEntry->entryname,
                        (char *)packet.payload + current_offset + OFFSET_READDIRX_PATH, sizeof(pEntry->entryname));

                    /*
                     Adjust our offset to point to the next entry within the packet
                     flags (1) + size (4) + mtime (4) + ctime (4) + null (1) = 14
                    */
                    current_offset += 14 + name_len;
                }
                else
                {
                    Debug_print("tnfs_readdirx Failed to allocate new dircache entry!\r\n");
                    break;
                }
            }

            int loaded = m_info->count_dircache();
#ifdef VERBOSE_TNFS
            Debug_printf("tnfs_readdirx cached %d entries\r\n", loaded);
#endif
            // Now that we've cached our entries, return the first one
            if(loaded > 0)
                _readdirx_fill_response(m_info->next_dircache_entry(), filestat, dir_entry, dir_entry_len);

        }
        return packet.payload[0];
    }
    return -1;
}

/*
    TELLDIR
*/
int tnfs_telldir(tnfsMountInfo *m_info, uint16_t *position)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    if(position == nullptr)
        return -1;

    // First see if we're pointing at a currently-cached directory entry and return that
    int cached = m_info->tell_dircache_entry();
    if (cached > -1)
    {
        *position = cached;
        return 0;
    }

    tnfsPacket packet;
    packet.command = TNFS_CMD_TELLDIR;
    packet.payload[0] = m_info->dir_handle;

    if (_tnfs_transaction(m_info, packet, 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            *position = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + 1);
        }
        return packet.payload[0];
    }
    return -1;
}

/*
    SEEKDIR
*/
int tnfs_seekdir(tnfsMountInfo *m_info, uint16_t position)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    // A SEEKDIR will always invalidate our directory cache
    m_info->empty_dircache();

    tnfsPacket packet;
    packet.command = TNFS_CMD_SEEKDIR;
    packet.payload[0] = m_info->dir_handle;
    uint32_t pos = position;
    TNFS_UINT32_TO_LOHI_BYTEPTR(pos, packet.payload + 1);

    if (_tnfs_transaction(m_info, packet, 5))
        return packet.payload[0];

    return -1;
}

/*
    Closes current directory handle specificed in tnfsMountInfo
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_closedir(tnfsMountInfo *m_info)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    // Throw out any existing cached directory entries
    m_info->empty_dircache();

    tnfsPacket packet;
    packet.command = TNFS_CMD_CLOSEDIR;
    packet.payload[0] = m_info->dir_handle;

    if (_tnfs_transaction(m_info, packet, 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            m_info->dir_handle = TNFS_INVALID_HANDLE;
        }
        return packet.payload[0];
    }
    return -1;
}

/*
    Creates directory.
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_mkdir(tnfsMountInfo *m_info, const char *directory)
{
    if (m_info == nullptr || directory == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_MKDIR;

    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload, directory, sizeof(packet.payload));

    Debug_printf("TNFS make directory: \"%s\"\r\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
    Deletes directory.
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_rmdir(tnfsMountInfo *m_info, const char *directory)
{
    if (m_info == nullptr || directory == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_RMDIR;

    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload, directory, sizeof(packet.payload));

    Debug_printf("TNFS remove directory: \"%s\"\r\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
    Returns file information filled in tnfsStat.
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_stat(tnfsMountInfo *m_info, tnfsStat *filestat, const char *filepath)
{
    if (m_info == nullptr || filepath == nullptr || filestat == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_STAT;

    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload, filepath, sizeof(packet.payload));

    //Debug_printf("TNFS stat: \"%s\"\r\n", (char *)packet.payload);

#define OFFSET_STAT_FILEMODE 1
#define OFFSET_STAT_UID 3
#define OFFSET_STAT_GID 5
#define OFFSET_STAT_FILESIZE 7
#define OFFSET_STAT_ATIME 11
#define OFFSET_STAT_MTIME 15
#define OFFSET_STAT_CTIME 19

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        __BEGIN_IGNORE_UNUSEDVARS
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {

            filestat->mode = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_FILEMODE);
            filestat->isDir = (filestat->mode & S_IFDIR) ? true : false;

            uint16_t uid = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_UID);
            uint16_t gid = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_GID);

            filestat->filesize = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_FILESIZE);

            filestat->a_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_ATIME);
            filestat->m_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_MTIME);
            filestat->c_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_CTIME);

            // Debug_printf("\ttnfs_stat: mode: %ho, uid: %hu, gid: %hu, dir: %d, size: %u, atime: 0x%04x, mtime: 0x%04x, ctime: 0x%04x\r\n",
            //     filestat->mode, uid, gid,
            //     filestat->isDir ? 1 : 0, filestat->filesize, filestat->a_time, filestat->m_time, filestat->c_time );

        }
        __END_IGNORE_UNUSEDVARS
        return packet.payload[0];
    }
    return -1;
}

/*
    Deletes file.
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_unlink(tnfsMountInfo *m_info, const char *filepath)
{
    if (m_info == nullptr || filepath == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_UNLINK;

    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload, filepath, sizeof(packet.payload));

    Debug_printf("TNFS unlink file: \"%s\"\r\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
    Renames file from old_filepath to new_filepath
    Relative paths ("../file") won't work.
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_rename(tnfsMountInfo *m_info, const char *old_filepath, const char *new_filepath)
{
    if (m_info == nullptr || old_filepath == nullptr || new_filepath == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_RENAME;

    int l1 = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload, old_filepath, sizeof(packet.payload)) + 1;
    int l2 = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload + l1, new_filepath, sizeof(packet.payload) - l1) + 1;

    Debug_printf("TNFS rename file: \"%s\" -> \"%s\"\r\n", (char *)packet.payload, (char *)(packet.payload + l1));

    if (_tnfs_transaction(m_info, packet, l1 + l2))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
    THIS ISN'T IMPLEMENTED IN THE CURRENT TNFSD CODE
    Changes permissions on file
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_chmod(tnfsMountInfo *m_info, const char *filepath, uint16_t mode)
{
    if (m_info == nullptr || filepath == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_CHMOD;

    packet.payload[0] = TNFS_LOBYTE_FROM_UINT16(mode);
    packet.payload[1] = TNFS_HIBYTE_FROM_UINT16(mode);

    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload + 2, filepath, sizeof(packet.payload) - 2);

    Debug_printf("TNFS chmod file: \"%s\", %ho\r\n", (char *)packet.payload + 2, mode);

    if (_tnfs_transaction(m_info, packet, len + 3))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
    THIS ISN'T IMPLEMENTED IN THE CURRENT TNFSD CODE
    Returns size of mounted filesystem in kilobytes in the 'size' parameter
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_size(tnfsMountInfo *m_info, uint32_t *size)
{
    if (m_info == nullptr || size == nullptr)
        return -1;

    tnfsPacket packet;

    packet.command = TNFS_CMD_SIZE;
    if (_tnfs_transaction(m_info, packet, 0))
    {
        if (packet.payload[0] == 0)
        {
            *size = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + 1);
        }
        return packet.payload[0];
    }

    return -1;
}

int tnfs_size_bytes(tnfsMountInfo *m_info, uint64_t *size)
{
    if (m_info == nullptr || size == nullptr)
        return -1;

    tnfsPacket packet;

    packet.command = TNFS_CMD_SIZE_BYTES;
    if (_tnfs_transaction(m_info, packet, 0))
    {
        if (packet.payload[0] == 0)
        {
            *size = TNFS_UINT64_FROM_LOHI_BYTEPTR(packet.payload + 1);
        }
        return packet.payload[0];
    }

    return -1;
}

/*
    THIS ISN'T IMPLEMENTED IN THE CURRENT TNFSD CODE
    Returns free kilobytes in mounted filesystem in the 'size' parameter
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_free(tnfsMountInfo *m_info, uint32_t *size)
{
    if (m_info == nullptr || size == nullptr)
        return -1;

    tnfsPacket packet;

    packet.command = TNFS_CMD_FREE;
    if (_tnfs_transaction(m_info, packet, 0))
    {
        if (packet.payload[0] == 0)
        {
            *size = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + 1);
        }
        return packet.payload[0];
    }

    return -1;
}

int tnfs_free_bytes(tnfsMountInfo *m_info, uint64_t *size)
{
    if (m_info == nullptr || size == nullptr)
        return -1;

    tnfsPacket packet;

    packet.command = TNFS_CMD_FREE_BYTES;
    if (_tnfs_transaction(m_info, packet, 0))
    {
        if (packet.payload[0] == 0)
        {
            *size = TNFS_UINT64_FROM_LOHI_BYTEPTR(packet.payload + 1);
        }
        return packet.payload[0];
    }

    return -1;
}

// ------------------------------------------------
// HELPER TNFS FUNCTIONS (Aren't actual TNFSD commands)
// ------------------------------------------------

/*
 Returns the filepath associated with an open filehandle
*/
const char *tnfs_filepath(tnfsMountInfo *m_info, int16_t file_handle)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(file_handle))
        return nullptr;

    // Find info on this handle
    const tnfsFileHandleInfo *pFileInf = m_info->get_filehandleinfo(file_handle);
    if (pFileInf == nullptr)
        return nullptr;

    return pFileInf->filename;
}

/*
 Sets the internally-tracked current working directory after confirming that
 the given directory exists.
 ".." can be used to go up one (and only one) directory
*/
int tnfs_chdir(tnfsMountInfo *m_info, const char *dirpath)
{
    if (m_info == nullptr || dirpath == nullptr)
        return -1;

    // Check for ".."
    if (dirpath[0] == '.' && dirpath[1] == '.' && dirpath[2] == '\0')
    {
        // Figure out what the previous directory is
        char *lslash = strrchr(m_info->current_working_directory, '/');
        // Assuming we're not alraedy at the root, just truncate the string at the last slash
        if (lslash != nullptr && lslash != m_info->current_working_directory)
            *lslash = '\0';
        return TNFS_RESULT_SUCCESS;
    }

    tnfsStat tstat;
    int rs = tnfs_stat(m_info, &tstat, dirpath);
    if (rs != TNFS_RESULT_SUCCESS)
        return rs;

    if (tstat.isDir == false)
        return TNFS_RESULT_NOT_A_DIRECTORY;

    // Looks okay - store it
    _tnfs_adjust_with_full_path(m_info, m_info->current_working_directory, dirpath, sizeof(m_info->current_working_directory));

    return TNFS_RESULT_SUCCESS;
}

/*
 Returns directory path we currently have stored
*/
const char *tnfs_getcwd(tnfsMountInfo *m_info)
{
    if (m_info == nullptr)
        return nullptr;
    return m_info->current_working_directory;
}
