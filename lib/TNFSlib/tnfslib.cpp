
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

                if (bytes_read > bytes_to_read)
                {
                    Debug_printf("_tnfs_fill_cache bogus read length %u > requested %u; rejecting\r\n",
                                 bytes_read, bytes_to_read);
                    error = -1;
                    break;
                }

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

