
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

typedef enum
{
    SUCCESS,
    FAILED,
    RESET,
} _tnfs_send_recv_result;

typedef enum
{
    RESP_VALID,
    RESP_INVALID,
    RESP_TRY_AGAIN,
    SESSION_RECOVERED,
    NO_RESP,
} _tnfs_recv_result;

bool _tnfs_transaction(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t datalen);
bool _tnfs_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size);
int _tnfs_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt);
bool _tnfs_tcp_send(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size);
int _tnfs_tcp_recv(tnfsMountInfo *m_info, tnfsPacket &pkt);
_tnfs_send_recv_result _tnfs_send_recv(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt);
_tnfs_recv_result _tnfs_recv_and_validate(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt);
uint8_t _tnfs_session_recovery(tnfsMountInfo *m_info, uint8_t command);

int _tnfs_adjust_with_full_path(tnfsMountInfo *m_info, char *buffer, const char *source, int bufflen);

void _tnfs_debug_packet(const tnfsPacket &pkt, unsigned short len, bool isResponse = false);

const char *_tnfs_command_string(int command);
const char *_tnfs_result_code_string(int resultcode);

using namespace std;

/* Logs-in to the TNFS server by providing a mount path, user and password.
 Success will result in a session ID set in tnfsMountInfo.
 If the host_ip is set, it will be used in all transactions instead of hostname.
 Currently, mountpath, userid and password are ignored.
 port, timeout_ms, and max_retries may be set or left to defaults.
 Returns:
  0 - success
 -1 - failure to send/receive command
 TNFS_RESULT_FUNCTION_UNIMPLEMENTED - returned server version lower than min requried
 other - TNFS_RESULT_*
*/
int tnfs_mount(tnfsMountInfo *m_info)
{
    if (m_info == nullptr)
        return -1;

    // Unmount if we happen to have sesssion
    if (m_info->session != TNFS_INVALID_SESSION)
        tnfs_umount(m_info);
    m_info->session = TNFS_INVALID_SESSION; // In case tnfs_umount fails - throw out the current session ID

    tnfsPacket packet;
    packet.command = TNFS_CMD_MOUNT;

    // TNFS VERSION
    packet.payload[0] = 0x00; // TNFS Version Minor (LSB)
    packet.payload[1] = 0x01; // TNFS Version Major (MSB)

    int payload_offset = 2;

    // If we weren't provided a mountpath, set the default
    if (m_info->mountpath[0] == '\0')
        m_info->mountpath[0] = '/';

    // Copy the mountpath to the payload
    strlcpy((char *)packet.payload + payload_offset, m_info->mountpath, sizeof(packet.payload) - payload_offset);
    payload_offset += strlen((char *)packet.payload + payload_offset) + 1;

    // Copy user
    strlcpy((char *)packet.payload + payload_offset, m_info->user, sizeof(packet.payload) - payload_offset);
    payload_offset += strlen((char *)packet.payload + payload_offset) + 1;

    // Copy password
    strlcpy((char *)packet.payload + payload_offset, m_info->password, sizeof(packet.payload) - payload_offset);
    payload_offset += strlen((char *)packet.payload + payload_offset) + 1;

    // Make sure we have the right starting working directory
    m_info->current_working_directory[0] = '/';

    if (_tnfs_transaction(m_info, packet, payload_offset))
    {
        // Success
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            m_info->session = TNFS_UINT16_FROM_HILOBYTES(packet.session_idh, packet.session_idl);
            m_info->server_version = TNFS_UINT16_FROM_HILOBYTES(packet.payload[2], packet.payload[1]);
            m_info->min_retry_ms = TNFS_UINT16_FROM_HILOBYTES(packet.payload[4], packet.payload[3]);

            // Check server version
            if(m_info->server_version < 0x0102)
            {
                Debug_printf("Server version 0x%04hx lower than minimum required\r\n", m_info->server_version);
                tnfs_umount(m_info);
                return TNFS_RESULT_FUNCTION_UNIMPLEMENTED;
            }
        }
        return packet.payload[0];
    }
    return -1;
}

/* Logs off TNFS server given data (session, host) in tnfsMountInfo
*/
int tnfs_umount(tnfsMountInfo *m_info)
{
    if (m_info == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_UNMOUNT;

    if (_tnfs_transaction(m_info, packet, 0))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            m_info->session = TNFS_INVALID_SESSION;
        }
        return packet.payload[0];
    }
    return -1;
}

/* Open a file
 open_mode: TNFS_OPENFLAG_*
 create_perms: TNFS_CREATEPERM_* (only meaningful when creating files)
 file_handle: if successful, server's file handle is stored here
 returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
*/
int tnfs_open(tnfsMountInfo *m_info, const char *filepath, uint16_t open_mode, uint16_t create_perms, int16_t *file_handle)
{
    if (m_info == nullptr || filepath == nullptr || file_handle == nullptr)
        return -1;

    *file_handle = TNFS_INVALID_HANDLE;

    // Find a free slot in our table of file handles
    tnfsFileHandleInfo *pFileInf = m_info->new_filehandleinfo();
    if (pFileInf == nullptr)
        return TNFS_RESULT_TOO_MANY_FILES_OPEN;

    // First, stat the file so we can get its length (if it exists), which we'll need to
    // keep track of the file position.
    bool file_exists = false;
    tnfsStat tstat;
    int rs = tnfs_stat(m_info, &tstat, filepath);
    // The only error we'll accept is TNFS_RESULT_FILE_NOT_FOUND, otherwise abort
    if (rs == TNFS_RESULT_SUCCESS)
    {
        file_exists = true;
        pFileInf->file_size = tstat.filesize;
    }
    else
    {
        if (rs != TNFS_RESULT_FILE_NOT_FOUND)
        {
            m_info->delete_filehandleinfo(pFileInf);
            return rs;
        }
    }

    // Done with STAT - now try to actually open the file
    tnfsPacket packet;
    packet.command = TNFS_CMD_OPEN;

    packet.payload[0] = TNFS_LOBYTE_FROM_UINT16(open_mode);
    packet.payload[1] = TNFS_HIBYTE_FROM_UINT16(open_mode);

    packet.payload[2] = TNFS_LOBYTE_FROM_UINT16(create_perms);
    packet.payload[3] = TNFS_HIBYTE_FROM_UINT16(create_perms);

    int offset_filename = 4; // Where the filename starts in the buffer

    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload + offset_filename, filepath, sizeof(packet.payload) - offset_filename);

    // Store the path we used as part of our file handle info
    strlcpy(pFileInf->filename, (const char *)&packet.payload[offset_filename], TNFS_MAX_FILELEN);

    Debug_printf("TNFS open file: \"%s\" (0x%04x, 0x%04x)\r\n", (char *)&packet.payload[offset_filename], open_mode, create_perms);

    // Offset to filename + filename length + zero terminator
    int result = -1;
    len = len + offset_filename + 1;
    if (_tnfs_transaction(m_info, packet, len))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            // Since everything went okay, save our file info
            pFileInf->handle_id = packet.payload[1];
            pFileInf->file_position = pFileInf->cached_pos = 0;

            *file_handle = pFileInf->handle_id;

            // Depending on the file mode and wether the file aready existed,
            // we need to do something different with the position of the file
            if (file_exists && (open_mode & TNFS_OPENMODE_WRITE))
            {
                if (open_mode & TNFS_OPENMODE_WRITE_APPEND)
                    pFileInf->file_position = pFileInf->cached_pos = pFileInf->file_size;
                else if (open_mode & TNFS_OPENMODE_WRITE_TRUNCATE)
                    pFileInf->file_size = 0;
            }
            Debug_printf("File opened, handle ID: %hd, size: %lu, pos: %lu\r\n", *file_handle, pFileInf->file_size, pFileInf->file_position);
        }
        result = packet.payload[0];
    }

    // Get rid fo the filehandleinfo if we're not going to use it
    if (result != TNFS_RESULT_SUCCESS)
        m_info->delete_filehandleinfo(pFileInf);

    return result;
}

/*
 Closes an open file
 returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
*/
int tnfs_close(tnfsMountInfo *m_info, int16_t file_handle)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(file_handle))
        return -1;

    // Find info on this handle
    tnfsFileHandleInfo *pFileInf = m_info->get_filehandleinfo(file_handle);
    if (pFileInf == nullptr)
        return TNFS_RESULT_BAD_FILE_DESCRIPTOR;

    tnfsPacket packet;
    packet.command = TNFS_CMD_CLOSE;
    packet.payload[0] = file_handle;

    if (_tnfs_transaction(m_info, packet, 1))
    {
        // We're going to go ahead and delete our info even though the server could reject it
        m_info->delete_filehandleinfo(pFileInf);
        return packet.payload[0];
    }

    return -1;
}

// #ifdef not needed, the linker optimization includes the code only if called from somewhere
void _tnfs_cache_dump(const char *title, uint8_t *cache, uint32_t cache_size)
{
    int bytes_per_line = 16;
    Debug_printf("\n%s %lu\r\n", title, cache_size);
    for (int j = 0; j < cache_size; j += bytes_per_line)
    {
        for (int k = 0; (k + j) < cache_size && k < bytes_per_line; k++)
            Debug_printf("%02X ", cache[k + j]);
        Debug_println("\r\n");
    }
    Debug_println("\r\n");
}

/*
 Fills destination buffer with data available in internal cache, if any
 Returns 0: success; TNFS_RESULT_END_OF_FILE: EOF; -1: if not all bytes requested could be fulfilled by cache
*/
int _tnfs_read_from_cache(tnfsFileHandleInfo *pFHI, uint8_t *dest, uint16_t dest_size, uint16_t *dest_used)
{
    #ifdef VERBOSE_TNFS
    Debug_printf("_tnfs_read_from_cache: buffpos=%lu, cache_start=%lu, cache_avail=%lu, dest_size=%u, dest_used=%u\r\n",
                 pFHI->cached_pos, pFHI->cache_start, pFHI->cache_available, dest_size, *dest_used);
    #endif

    // Report if we've reached the end of the file
    if (pFHI->cached_pos >= pFHI->file_size)
    {
        #ifdef VERBOSE_TNFS
        Debug_print("_tnfs_read_from_cache - attempting to read past EOF\r\n");
        #endif
        return TNFS_RESULT_END_OF_FILE;
    }
    // Reject if we have nothing in the cache
    if (pFHI->cache_available == 0)
    {
        #ifdef VERBOSE_TNFS
        Debug_print("_tnfs_read_from_cache - nothing in cache\r\n");
        #endif
        return -1;
    }

    // See if the current file position is at or after the start of our cache
    if (pFHI->cached_pos >= pFHI->cache_start)
    {
        // See if the current file position falls before the end of our cache
        uint32_t cache_end = pFHI->cache_start + pFHI->cache_available;
        if (pFHI->cached_pos < cache_end)
        {
            // Our current file position is within the cached region
            // Calculate how many bytes to provide:
            // Either from the position to the end of the cache
            // Or the bytes free at the destination if that's smaller
            uint32_t bytes_available = cache_end - pFHI->cached_pos;
            uint16_t dest_free = dest_size - *dest_used; // This accounts for an earlier partially-fulfilled request
            uint16_t bytes_provided = dest_free > bytes_available ? bytes_available : dest_free;

            #ifdef VERBOSE_TNFS
            Debug_printf("TNFS cache providing %u bytes\r\n", bytes_provided);
            #endif
            memcpy(dest + (*dest_used), pFHI->cache + (pFHI->cached_pos - pFHI->cache_start), bytes_provided);

#ifdef DEBUG
            //_tnfs_cache_dump("CACHE PROVIDED", dest + (*dest_used), bytes_provided);
#endif

            pFHI->cached_pos += bytes_provided;
            *dest_used += bytes_provided;

            /*
            // Report if we've reached the end of the file
            if (pFHI->cached_pos > pFHI->file_size)
            {
                Debug_print("_tnfs_read_from_cache - reached EOF\r\n");
                return TNFS_RESULT_END_OF_FILE;
            }
            */
        }
    }

    // Return value depends on whether we filled the destination buffer
    if (dest_size - *dest_used)
        return -1;
    else
        return 0;
}

/*
 Executes as many READ calls as needed to populate our internal cache
 Returns: 0: success; -1: failed to deliver/receive packet; other: TNFS error result code
*/
int _tnfs_fill_cache(tnfsMountInfo *m_info, tnfsFileHandleInfo *pFHI)
{
    // Note that when we're filling the cache, we're dealing with the "real" file position,
    // not the cached_position we also keep track of on behalf of the client
    #ifdef VERBOSE_TNFS
    Debug_printf("_TNFS_FILL_CACHE fh=%d, file_position=%lu\r\n", pFHI->handle_id, pFHI->file_position);
    #endif

    int error = 0;

    // Reset the current cache values so it's invalid if we fail below
    pFHI->cache_available = 0;
    pFHI->cache_start = pFHI->file_position;

    // How many bytes until we finish loading the cache
    uint32_t bytes_remaining_to_load = sizeof(pFHI->cache);

    // Keep making TNFS READ calls as long as we still have bytes to read
    while (bytes_remaining_to_load > 0)
    {
        tnfsPacket packet;
        packet.command = TNFS_CMD_READ;
        packet.payload[0] = pFHI->handle_id;

        // How many bytes to read in this call
        uint16_t bytes_to_read = bytes_remaining_to_load > TNFS_MAX_READWRITE_PAYLOAD ? TNFS_MAX_READWRITE_PAYLOAD : bytes_remaining_to_load;

        packet.payload[1] = TNFS_LOBYTE_FROM_UINT16(bytes_to_read);
        packet.payload[2] = TNFS_HIBYTE_FROM_UINT16(bytes_to_read);

        #ifdef VERBOSE_TNFS
        Debug_printf("_tnfs_fill_cache requesting %u bytes\r\n", bytes_to_read);
        #endif

        if (_tnfs_transaction(m_info, packet, 3))
        {
            int tnfs_result = packet.payload[0];
            if (tnfs_result == TNFS_RESULT_SUCCESS)
            {
                // Copy the actual number of bytes returned to us into our cache
                // (offset by how many bytes we've already put in the cache)
                uint16_t bytes_read = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + 1);
                memcpy(pFHI->cache + (sizeof(pFHI->cache) - bytes_remaining_to_load),
                       packet.payload + 3, bytes_read);

                // Keep track of our file position
                pFHI->file_position = pFHI->file_position + bytes_read;
                // Keep track of how many more bytes we have to go
                bytes_remaining_to_load -= bytes_read;

                #ifdef VERBOSE_TNFS
                Debug_printf("_tnfs_fill_cache got %u bytes, %lu more bytes needed\r\n", bytes_read, bytes_remaining_to_load);
                #endif
            }
            else if(tnfs_result == TNFS_RESULT_END_OF_FILE)
            {
                // Stop if we got an EOF result
                #ifdef VERBOSE_TNFS
                Debug_print("_tnfs_fill_cache got EOF\r\n");
                #endif
#ifndef ESP_PLATFORM
// TODO review EOF handling
                error = TNFS_RESULT_END_OF_FILE; // push EOF up
#endif
                break;
            }
            else
            {
                Debug_printf("_tnfs_fill_cache unexepcted result: %u\r\n", tnfs_result);
                error = tnfs_result;
                break;
            }
        }
        else
        {
            Debug_print("_tnfs_fill_cache received failure condition on TNFS read attempt\r\n");
            error = -1;
            break;
        }
    }

    // If we're successful, note the total number of valid bytes in our cache
#ifdef ESP_PLATFORM
    if (error == 0)
    {
        pFHI->cache_available = sizeof(pFHI->cache) - bytes_remaining_to_load;
#else
// TODO review EOF handling
    if (error == 0 || error == TNFS_RESULT_END_OF_FILE)
    {
        pFHI->cache_available = sizeof(pFHI->cache) - bytes_remaining_to_load;
        if (pFHI->cache_available > 0) error = 0; // neutralize EOF
#endif
#ifdef DEBUG
        //_tnfs_cache_dump("CACHE FILL RESULTS", pFHI->cache, pFHI->cache_available);
#endif
    }

    return error;
}

/*
 Reads from an open file.
 Max bufflen is TNFS_PAYLOAD_SIZE - 3; any larger size will return an error
 Bytes actually read will be placed in resultlen
 Returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
 */
int tnfs_read(tnfsMountInfo *m_info, int16_t file_handle, uint8_t *buffer, uint16_t bufflen, uint16_t *resultlen)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(file_handle) ||
        buffer == nullptr || bufflen > (TNFS_PAYLOAD_SIZE - 3) || resultlen == nullptr)
        return -1;

    *resultlen = 0;

    // Find info on this handle
    tnfsFileHandleInfo *pFileInf = m_info->get_filehandleinfo(file_handle);
    if (pFileInf == nullptr)
        return TNFS_RESULT_BAD_FILE_DESCRIPTOR;

    #ifdef VERBOSE_TNFS
    Debug_printf("tnfs_read fh=%d, len=%d\r\n", file_handle, bufflen);
    #endif

    int result = 0;
    // Try to fulfill the request using our internal cache
    while ((result = _tnfs_read_from_cache(pFileInf, buffer, bufflen, resultlen)) != 0 && result != TNFS_RESULT_END_OF_FILE)
    {
        // Reload the cache if we couldn't fulfill the request
        result = _tnfs_fill_cache(m_info, pFileInf);
        if (result != 0)
        {
#ifndef ESP_PLATFORM
// TODO review EOF handling
            if (result == TNFS_RESULT_END_OF_FILE)
            {
                Debug_println("tnfs_read empty cache got EOF");
                if (pFileInf->cached_pos < pFileInf->file_size)
                {
                    Debug_printf("tnfs_read premature end of file, got %u, expected %u\n", (unsigned)pFileInf->cached_pos, (unsigned)pFileInf->file_size);
                }
            }
            else
#endif
            {
                Debug_printf("tnfs_read cache fill failed (%u) - aborting\n", result);
            }
            break;
        }
    }

    return result;
}


/*
 Write to an open file.
 Max bufflen is TNFS_PAYLOAD_SIZE - 3; any larger size will return an error
 Bytes actually written will be placed in resultlen
 Returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
 */
int tnfs_write(tnfsMountInfo *m_info, int16_t file_handle, uint8_t *buffer, uint16_t bufflen, uint16_t *resultlen)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(file_handle) ||
        buffer == nullptr || bufflen > (TNFS_PAYLOAD_SIZE - 3) || resultlen == nullptr)
        return -1;

    *resultlen = 0;

    // Find info on this handle
    tnfsFileHandleInfo *pFileInf = m_info->get_filehandleinfo(file_handle);
    if (pFileInf == nullptr)
        return TNFS_RESULT_BAD_FILE_DESCRIPTOR;

    // For now, invalidate our cache and seek to the current position in the file before writing
    pFileInf->cache_available = 0;
    if(pFileInf->cached_pos != pFileInf->file_position)
    {
        int result = tnfs_lseek(m_info, file_handle, pFileInf->cached_pos, SEEK_SET, nullptr, true);
        if(result != 0)
        {
            Debug_print("TNFS seek failed during write\r\n");
            return result;
        }
    }

    tnfsPacket packet;
    packet.command = TNFS_CMD_WRITE;
    packet.payload[0] = file_handle;
    packet.payload[1] = TNFS_LOBYTE_FROM_UINT16(bufflen);
    packet.payload[2] = TNFS_HIBYTE_FROM_UINT16(bufflen);

    memcpy(packet.payload + 3, buffer, bufflen);

    if (_tnfs_transaction(m_info, packet, bufflen + 3))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            *resultlen = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + 1);
            // Keep track of our file position
            uint32_t new_pos = pFileInf->file_position + *resultlen;
            // Debug_printf("tnfs_write prev_pos: %u, read: %u, new_pos: %u\r\n", pFileInf->file_position, *resultlen, new_pos);
            pFileInf->file_position = pFileInf->cached_pos = new_pos;
        }
        return packet.payload[0];
    }
    return -1;
}

/*
  Try to seek within our internal cache
  Return 0 on success, -1 on failure
*/
int _tnfs_cache_seek(tnfsFileHandleInfo *pFHI, int32_t position, uint8_t type)
{
    if (pFHI->cache_available == 0)
        return -1;

    // Calculate where we're supposed to end up to see if it's within the cached region
    uint32_t destination_pos;
    if (type == SEEK_SET)
        destination_pos = position;
    else if (type == SEEK_CUR)
        destination_pos = pFHI->cached_pos + position;
    else
        destination_pos = pFHI->file_size + position;

    uint32_t cache_end = pFHI->cache_start + pFHI->cache_available;
#ifdef TNFS_DEBUG
    Debug_printf("_tnfs_cache_seek current=%u, destination=%u, cache_start=%u, cache_end=%u\r\n",
                 pFHI->cached_pos, destination_pos, pFHI->cache_start, cache_end);
#endif

    // Just update our position if we're within the cached region
    if (destination_pos >= pFHI->cache_start && destination_pos < cache_end)
    {
#ifdef TNFS_DEBUG
        Debug_println("_tnfs_cache_seek within cached region");
#endif
        pFHI->cached_pos = destination_pos;
        return 0;
    }
#ifdef TNFS_DEBUG
    Debug_println("_tnfs_cache_seek outside cached region");
#endif
    return -1;
}

/*
 Seek to different position in open file
 Returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
 */
int tnfs_lseek(tnfsMountInfo *m_info, int16_t file_handle, int32_t position, uint8_t type, uint32_t *new_position, bool skip_cache)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(file_handle))
        return -1;

    // Make sure we're using a valid seek type
    if (type != SEEK_SET && type != SEEK_CUR && type != SEEK_END)
        return TNFS_RESULT_INVALID_ARGUMENT;

    // Find info on this handle
    tnfsFileHandleInfo *pFileInf = m_info->get_filehandleinfo(file_handle);
    if (pFileInf == nullptr)
        return TNFS_RESULT_BAD_FILE_DESCRIPTOR;

#ifdef TNFS_DEBUG
    Debug_printf("tnfs_lseek currpos=%d, pos=%d, typ=%d\r\n", pFileInf->cached_pos, position, type);
#endif

    // Try to fulfill the seek within our internal cache
    if (skip_cache == false && _tnfs_cache_seek(pFileInf, position, type) == 0)
    {
        if(new_position != nullptr)
            *new_position = pFileInf->cached_pos;
        return 0;
    }
    // Cache seek failed - invalidate the internal cache
    pFileInf->cache_available = 0;

    // Go ahead and execute a new TNFS SEEK request
    tnfsPacket packet;
    packet.command = TNFS_CMD_LSEEK;
    packet.payload[0] = file_handle;
    packet.payload[1] = type;
    TNFS_UINT32_TO_LOHI_BYTEPTR(position, packet.payload + 2);

    if (_tnfs_transaction(m_info, packet, 6))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            // Keep track of our file position
            if (type == SEEK_SET)
                pFileInf->file_position = position;
            else if (type == SEEK_CUR)
                pFileInf->file_position += position;
            else
                pFileInf->file_position = (pFileInf->file_size + position);

            pFileInf->cached_pos = pFileInf->file_position;

            if(new_position != nullptr)
                *new_position = pFileInf->file_position;
            uint32_t response_pos = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + 1);
#ifdef TNFS_DEBUG
            Debug_printf("tnfs_lseek success, new pos=%u, response pos=%u\r\n", pFileInf->file_position, response_pos);
#endif
            // TODO: This is temporary while we confirm that the recently-changed TNFSD code matches what we've been doing prior
            if(pFileInf->file_position != response_pos)
            {
                Debug_print("CALCULATED AND RESPONSE POS DON'T MATCH!\r\n");
                fnSystem.delay(5000);
            }
        }
        return packet.payload[0];
    }
    return -1;
}

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
// ------------------------------------------------
// INTERNAL UTILITY FUNCTIONS
// ------------------------------------------------

/*
    Send the packet, using UDP or TCP, depending on the m_info.
    Reports whether the operation was successful.

    If TCP is used and there's no connection, try to connect first.
*/
bool _tnfs_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    if (m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        bool success = _tnfs_tcp_send(m_info, pkt, payload_size);
        if (!success)
        {
            Debug_println("Can't connect to the TCP server; falling back to UDP.");
            m_info->protocol = TNFS_PROTOCOL_UDP;
            return _tnfs_udp_send(udp, m_info, pkt, payload_size);
        }
        return success;
    }
    else if (m_info->protocol == TNFS_PROTOCOL_TCP)
    {
        return _tnfs_tcp_send(m_info, pkt, payload_size);
    }
    else
    {
        return _tnfs_udp_send(udp, m_info, pkt, payload_size);
    }
}

bool _tnfs_tcp_send(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    fnTcpClient *tcp = &m_info->tcp_client;
    if (!tcp->connected())
    {
        bool success = false;
        if (m_info->host_ip != IPADDR_NONE)
            success = tcp->connect(m_info->host_ip, m_info->port, TNFS_TIMEOUT);
        else
        {
            success = tcp->connect(m_info->hostname, m_info->port, TNFS_TIMEOUT);
            m_info->host_ip = tcp->remoteIP();
        }
        if (!success)
        {
            Debug_println("Can't connect to the TCP server");
            return false;
        }
    }
    int l = tcp->write(pkt.rawData, payload_size + TNFS_HEADER_SIZE);
    return l == payload_size + TNFS_HEADER_SIZE;
}

#ifndef TNFS_UDP_SIMULATE_POOR_CONNECTION
bool _tnfs_udp_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    return _tnfs_udp_do_send(udp, m_info, pkt, payload_size);
}
#endif

bool _tnfs_udp_do_send(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    bool sent;
    // Use the IP address if we have it
    if (m_info->host_ip != IPADDR_NONE)
        sent = udp->beginPacket(m_info->host_ip, m_info->port);
    else
    {
        sent = udp->beginPacket(m_info->hostname, m_info->port);
        m_info->host_ip = udp->remoteIP();
    }

    if (sent)
    {
        udp->write(pkt.rawData, payload_size + TNFS_HEADER_SIZE); // Add the data payload along with 4 bytes of TNFS header
        sent = udp->endPacket();
    }
    return sent;
}

/*
    Receive the packet, using UDP or TCP, depending on the m_info.
    Return the number of received bytes or an negative value if the
    packet is not available or an error occurred.
*/
int _tnfs_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt)
{
    if (m_info->protocol == TNFS_PROTOCOL_TCP || m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        return _tnfs_tcp_recv(m_info, pkt);
    }
    else
    {
        return _tnfs_udp_recv(udp, m_info, pkt);
    }
}

int _tnfs_tcp_recv(tnfsMountInfo *m_info, tnfsPacket &pkt)
{
    fnTcpClient *tcp = &m_info->tcp_client;
    if (!tcp->connected())
    {
        return -1;
    }
    if (!tcp->available())
    {
        return -1;
    }
    return tcp->read(pkt.rawData, sizeof(pkt.rawData));
}

#ifndef TNFS_UDP_SIMULATE_POOR_CONNECTION
int _tnfs_udp_recv(fnUDP *udp, tnfsMountInfo *m_info, tnfsPacket &pkt)
{
    if (!udp->parsePacket())
    {
        return -1;
    }
    int len = udp->read(pkt.rawData, sizeof(pkt.rawData));
    return len;
}
#endif

/*
  Send constructed TNFS packet and check for reply
  The send/receive loop will be attempted tnfsPacket.max_retries times (default: TNFS_RETRIES)
  Each retry attempt is limited to tnfsPacket.timeout_ms (default: TNFS_TIMEOUT)

  Only the command (tnfsPacket.command) and payload contents need to be set on the packet.
  Current session ID will be copied from tnfsMountInfo and retryCount is always reset to zero.

  If successful, server's response code will be the first byte of of tnfsPacket.data

  returns - true if response packet was received
            false if no response received during retries/timeout period
 */
bool _tnfs_transaction(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    std::lock_guard<std::recursive_mutex> lock(m_info->transaction_mutex);

    fnUDP udp;

    // Set our session ID
    tnfsPacket reqPkt = pkt;
    reqPkt.session_idl = TNFS_LOBYTE_FROM_UINT16(m_info->session);
    reqPkt.session_idh = TNFS_HIBYTE_FROM_UINT16(m_info->session);

    // Set sequence number before the transaction loop
    reqPkt.sequence_num = m_info->current_sequence_num++;

    // Start a new retry sequence
    for (int retry = 0; retry < m_info->max_retries; retry++)
    {
        switch(_tnfs_send_recv(udp, m_info, reqPkt, payload_size, pkt))
        {
            case SUCCESS:
            return true;

            case RESET:
            retry = -1;
            continue;

            case FAILED:
            default:
            // fallback to retry
            break;
        }
        
        // Make sure we wait before retrying
        fnSystem.delay(m_info->min_retry_ms);
    }

    Debug_printf("Retry attempts failed for host: %s, path: %s, cwd: %s\r\n", m_info->hostname, m_info->mountpath, m_info->current_working_directory);

    return false;
}

_tnfs_send_recv_result _tnfs_send_recv(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt)
{
#ifdef DEBUG
    _tnfs_debug_packet(req_pkt, payload_size);
#endif

    // Send packet
    bool sent = _tnfs_send(&udp, m_info, req_pkt, payload_size);
    if (!sent)
    {
        Debug_println("Failed to send packet - retrying");
        return FAILED;
    }

    // Wait for a response at most TNFS_TIMEOUT milliseconds
#ifdef ESP_PLATFORM
    int ms_start = fnSystem.millis();
#else
    uint64_t ms_start = fnSystem.millis();
#endif
    do
    {
        if (SYSTEM_BUS.getShuttingDown())
        {
            Debug_println("TNFS Breakout due to Shutdown");
            return SUCCESS; // false success just to get out
        }

        switch(_tnfs_recv_and_validate(udp, m_info, req_pkt, payload_size, res_pkt))
        {
            case RESP_VALID:
#ifndef ESP_PLATFORM
            Debug_printf("_tnfs_transaction completed in %u ms\n", (unsigned)(fnSystem.millis() - ms_start));
#endif
            return SUCCESS;

            case RESP_TRY_AGAIN:
            return FAILED;

            case RESP_INVALID:
            return FAILED;

            case SESSION_RECOVERED:
            return RESET;

            case NO_RESP:
            default:
            break;
        }

#ifdef ESP_PLATFORM
        fnSystem.yield();
#else
        fnSystem.delay_microseconds(5000); // wait more time for (remote) data to arrive
#endif

    } while ((fnSystem.millis() - ms_start) < m_info->timeout_ms); // packet receive loop

    if (m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        // This is probably an old tcpd server, accepting TCP connections but not responding
        // to any commands. We should fall back to UDP too and don't count this iteration
        // in the retry counter.
        Debug_println("No response to TCP mount request; falling back to UDP.");
        m_info->protocol = TNFS_PROTOCOL_UDP;
        return RESET;
    }
    
    Debug_printf("Timeout after %d milliseconds. Retrying\r\n", m_info->timeout_ms);
    return FAILED;
}

_tnfs_recv_result _tnfs_recv_and_validate(fnUDP &udp, tnfsMountInfo *m_info, tnfsPacket &req_pkt, uint16_t payload_size, tnfsPacket &res_pkt)
{
#ifndef ESP_PLATFORM
    fnSystem.delay_microseconds(2000); // wait short time for (local) data to arrive
#endif
    int l = _tnfs_recv(&udp, m_info, res_pkt);
    if (l < 0)
    {
        return NO_RESP;
    }
#ifdef DEBUG
    _tnfs_debug_packet(res_pkt, l, true);
#endif
    if (m_info->protocol == TNFS_PROTOCOL_UNKNOWN)
    {
        Debug_println("TNFS server supports TCP.");
        m_info->protocol = TNFS_PROTOCOL_TCP;
    }

    // Delayed response for the previous request. We should just try to recv the next response.
    if (res_pkt.sequence_num < req_pkt.sequence_num)
    {
        Debug_printf("Received delayed response! Rcvd: %x, Expected: %x\r\n", res_pkt.sequence_num, req_pkt.sequence_num);
        return NO_RESP;
    }

    // Out of order packet received.
    if (res_pkt.sequence_num != req_pkt.sequence_num)
    {
        Debug_printf("TNFS OUT OF ORDER SEQUENCE! Rcvd: %x, Expected: %x\r\n", res_pkt.sequence_num, req_pkt.sequence_num);
        return RESP_INVALID;
    }

    // Check in case the server asks us to wait and try again
    if (res_pkt.payload[0] == TNFS_RESULT_TRY_AGAIN)
    {
        // Server should tell us how long it wants us to wait
        uint16_t backoffms = TNFS_UINT16_FROM_LOHI_BYTEPTR(res_pkt.payload + 1);
        Debug_printf("Server asked us to TRY AGAIN after %ums\r\n", backoffms);
        if (backoffms > TNFS_MAX_BACKOFF_DELAY)
            backoffms = TNFS_MAX_BACKOFF_DELAY;
        fnSystem.delay(backoffms);
        return RESP_TRY_AGAIN;
    }

    // Check for invalid (expired) session
    if (res_pkt.payload[0] == TNFS_RESULT_INVALID_HANDLE \
                && req_pkt.command != TNFS_CMD_MOUNT \
                && req_pkt.command != TNFS_CMD_UNMOUNT)
    {
        Debug_printf("_tnfs_transaction - Invalid session ID\n");
        // Recovery - start new session with server, i.e. remount
        uint8_t res = _tnfs_session_recovery(m_info, req_pkt.command);
        if (res != TNFS_RESULT_SUCCESS)
        {
            // update the result byte (TNFS_RESULT_INVALID_HANDLE or TNFS_RESULT_BAD_FILENUM)
            res_pkt.payload[0] = res;
            return RESP_VALID;
        }
        // retry the command using new session
        req_pkt.session_idl = TNFS_LOBYTE_FROM_UINT16(m_info->session);
        req_pkt.session_idh = TNFS_HIBYTE_FROM_UINT16(m_info->session);
        return SESSION_RECOVERED;
    }

    return RESP_VALID;
}

// Re-mount using provided tnfsMountInfo*
// Returns TNFS result code
uint8_t _tnfs_session_recovery(tnfsMountInfo *m_info, uint8_t command)
{
    m_info->session = TNFS_INVALID_SESSION; // prevent umount call
    if (tnfs_mount(m_info) != TNFS_RESULT_SUCCESS)
    {
        Debug_printf("_tnfs_session_recovery - remount failed\n");
        return TNFS_RESULT_INVALID_HANDLE;
    }
    // re-mount succeeded, check the command
    switch (command)
    {
    case TNFS_CMD_OPENDIR:
    case TNFS_CMD_MKDIR:
    case TNFS_CMD_RMDIR:
    case TNFS_CMD_OPENDIRX:
    case TNFS_CMD_STAT:
    case TNFS_CMD_UNLINK:
    case TNFS_CMD_CHMOD:
    case TNFS_CMD_RENAME:
    case TNFS_CMD_OPEN:
    case TNFS_CMD_SIZE:
    case TNFS_CMD_FREE:
        // session was recovered and specified command can be retried within new session
        return TNFS_RESULT_SUCCESS;
    }
    // all other commands requires file descriptor or handle (which is lost with expired session)
    return TNFS_RESULT_BAD_FILENUM;
}

// Copies to buffer while ensuring that we start with a '/'
// Returns length of new full path or -1 on failure
int _tnfs_adjust_with_full_path(tnfsMountInfo *m_info, char *buffer, const char *source, int bufflen)
{
    if (buffer == nullptr || bufflen < 2)
        return -1;

    // Use the cwd to bulid the full path
    strlcpy(buffer, m_info->current_working_directory, bufflen);

    // Figure out whether or not we need to add a slash
    if (source[0] == '/') {
        // Ensure it fits in the buffer
        if ((int)strlen(source) >= bufflen)
            return -1;
        strlcpy(buffer, source, bufflen);
        return strlen(buffer);
    }

    // Figure out whether or not we need to add a slash
    int ll;
    ll = strlen(buffer);
    if (ll < 1 || ll > (bufflen - 2))
        return -1;

    bool dir_slash = buffer[ll - 1] == '/';
    bool needs_slash = source[0] != '/';
    if (needs_slash && dir_slash == false)
    {
        buffer[ll] = '/';
        buffer[++ll] = '\0';
    }
    if (needs_slash == false && dir_slash)
    {
        buffer[--ll] = '\0';
    }

    // Finally copy the source filepath
    strlcpy(buffer + ll, source, bufflen - ll);

    // And return the new length because that ends up being useful
    return strlen(buffer);
}

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
    case TNFS_RESULT_PROTOCOL_ERROR:
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
    case TNFS_RESULT_PROTOCOL_ERROR:
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
