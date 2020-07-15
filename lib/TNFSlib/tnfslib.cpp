#include <memory>
#include <string.h>

#include "tnfslib.h"
#include "../tcpip/fnUDP.h"
#include "../utils/utils.h"
#include "../hardware/fnSystem.h"

bool _tnfs_transaction(tnfsMountInfo *m_info, tnfsPacket &pkt, uint16_t datalen);

int _tnfs_adjust_with_full_path(tnfsMountInfo *m_info, char *buffer, const char *source, int bufflen);

void _tnfs_debug_packet(const tnfsPacket &pkt, unsigned short len, bool isResponse = false);

const char *_tnfs_command_string(int command);
const char *_tnfs_result_code_string(int resultcode);

using namespace std;

/*
MOUNT - Command ID 0x00
-----------------------

Format:
Standard header followed by:
Bytes 4+: 16 bit version number, little endian, LSB = minor, MSB = major
          NULL terminated string: mount location
          NULL terminated string: user id (optional - NULL if no user id)
          NULL terminated string: password (optional - NULL if no passwd)

Example:

To mount /home/tnfs on the server, with user id "example" and password of
"password", using version 1.2 of the protocol:
0x0000 0x00 0x00 0x02 0x01 /home/tnfs 0x00 example 0x00 password 0x00

To mount "a:" anonymously, using version 1.2 of the protocol:
0x0000 0x00 0x00 0x02 0x01 a: 0x00 0x00 0x00

The server responds with the standard header. If the operation was successful,
the standard header contains the session number, and the TNFS protocol
version that the server is using following the header, followed by the
minimum retry time in milliseconds as a little-endian 16 bit number.
Clients must respect this minimum retry value, especially for a server
with a slow underlying file system such as a floppy disc, to avoid swamping
the server. A client should also never have more than one request "in flight"
at any one time for any operation where order is important, so for example,
if reading a file, don't send a new request to read from a given file handle
before completing the last request.

Example: A successful MOUNT command was carried out, with a server that
supports version 2.6, and has a minimum retry time of 5 seconds (5000 ms,
hex 0x1388). Session ID is 0xBEEF:

0xBEEF 0x00 0x00 0x00 0x06 0x02 0x88 0x13

Example: A failed MOUNT command with error 1F for a version 3.5 server:
0x0000 0x00 0x00 0x1F 0x05 0x03
*/
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
    strncpy((char *)packet.payload + payload_offset, m_info->mountpath, sizeof(packet.payload) - payload_offset);
    payload_offset += strlen((char *)packet.payload + payload_offset) + 1;

    // Copy user
    strncpy((char *)packet.payload + payload_offset, m_info->user, sizeof(packet.payload) - payload_offset);
    payload_offset += strlen((char *)packet.payload + payload_offset) + 1;

    // Copy password
    strncpy((char *)packet.payload + payload_offset, m_info->password, sizeof(packet.payload) - payload_offset);
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
            // TODO: Return error on bad version
            if(m_info->server_version < 0x0102)
            {
                Debug_printf("Server version 0x%04hx lower than minimum required\n", m_info->server_version);
                // tnfs_umount(m_info);
                // return TNFS_RESULT_FUNCTION_UNIMPLEMENTED;
            }
        }
        return packet.payload[0];
    }
    return -1;
}

/*
UMOUNT - Command ID 0x01
------------------------

Format:
Standard header only, containing the connection ID to terminate, 0x00 as
the sequence number, and 0x01 as the command.

Example:
To UMOUNT the filesystem mounted with id 0xBEEF:

0xBEEF 0x00 0x01

The server responds with the standard header and a return code as byte 4.
The return code is 0x00 for OK. Example:

0xBEEF 0x00 0x01 0x00

On error, byte 4 is set to the error code, for example, for error 0x1F:

0xBEEF 0x00 0x01 0x1F
*/
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

/*
OPEN - Opens a file - Command 0x29
----------------------------------
Format: Standard header, flags, mode, then the null terminated filename.
Flags are a bit field.

The flags are:
TNFS_OPENMODE_READ             0x0001 // Open read only
TNFS_OPENMODE_WRITE            0x0002 // Open write only
TNFS_OPENMODE_READWRITE        0x0003 // Open read/write
TNFS_OPENMODE_WRITE_APPEND     0x0008 // Append to the file if it exists (write only)
TNFS_OPENMODE_WRITE_CREATE     0x0100 // Create the file if it doesn't exist (write only)
TNFS_OPENMODE_WRITE_TRUNCATE   0x0200 // Truncate the file on open for writing
TNFS_OPENMODE_CREATE_EXCLUSIVE 0x0400 // With TNFS_OPENMODE_CREATE, returns an error if the file exists

The modes are the same as described by CHMOD (i.e. POSIX modes). These
may be modified by the server process's umask. The mode only applies
when files are created (if the O_CREAT flag is specified)

Examples: 
Open a file called "/foo/bar/baz.bas" for reading:

0xBEEF 0x00 0x29 0x0001 0x0000 /foo/bar/baz.bas 0x00

Open a file called "/tmp/foo.dat" for writing, creating the file but
returning an error if it exists. Modes set are S_IRUSR, S_IWUSR, S_IRGRP
and S_IWOTH (read/write for owner, read-only for group, read-only for
others):

0xBEEF 0x00 0x29 0x0102 0x01A4 /tmp/foo.dat 0x00

The server returns the standard header and a result code in response.
If the operation was successful, the byte following the result code
is the file descriptor:

0xBEEF 0x00 0x29 0x00 0x04 - Successful file open, file descriptor = 4
0xBEEF 0x00 0x29 0x01 - File open failed with "permssion denied"
*/
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
    strncpy(pFileInf->filename, (const char *)&packet.payload[offset_filename], TNFS_MAX_FILELEN);

    Debug_printf("TNFS open file: \"%s\" (0x%04x, 0x%04x)\n", (char *)&packet.payload[offset_filename], open_mode, create_perms);

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
            Debug_printf("File opened, handle ID: %hhd, size: %u, pos: %u\n", *file_handle, pFileInf->file_size, pFileInf->file_position);
        }
        result = packet.payload[0];
    }

    // Get rid fo the filehandleinfo if we're not going to use it
    if (result != TNFS_RESULT_SUCCESS)
        m_info->delete_filehandleinfo(pFileInf);

    return result;
}

/*
CLOSE - Closes a file - Command 0x23
------------------------------------
Closes an open file. Consists of the standard header, followed by
the file descriptor. Example:

0xBEEF 0x00 0x23 0x04 - Close file descriptor 4

The server replies with the standard header followed by the return
code:

0xBEEF 0x00 0x23 0x00 - File closed.
0xBEEF 0x00 0x23 0x06 - Operation failed with EBADF, "bad file descriptor"
*/
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

#ifdef DEBUG
void _tnfs_cache_dump(const char *title, uint8_t *cache, uint32_t cache_size)
{
    int bytes_per_line = 16;
    Debug_printf("\n%s %u\n", title, cache_size);
    for (int j = 0; j < cache_size; j += bytes_per_line)
    {
        for (int k = 0; (k + j) < cache_size && k < bytes_per_line; k++)
            Debug_printf("%02X ", cache[k + j]);
        Debug_println();
    }
    Debug_println();
}
#endif    

/*
 Fills destination buffer with data available in internal cache, if any
 Returns 0: success; TNFS_RESULT_END_OF_FILE: EOF; -1: if not all bytes requested could be fulfilled by cache
*/
int _tnfs_read_from_cache(tnfsFileHandleInfo *pFHI, uint8_t *dest, uint16_t dest_size, uint16_t *dest_used)
{
    Debug_printf("_tnfs_read_from_cache: buffpos=%d, cache_start=%d, cache_avail=%d, dest_size=%d, dest_used=%d\n",
                 pFHI->cached_pos, pFHI->cache_start, pFHI->cache_available, dest_size, *dest_used);

    // Report if we've reached the end of the file
    if (pFHI->cached_pos >= pFHI->file_size)
    {
        Debug_print("_tnfs_read_from_cache - attempting to read past EOF\n");
        return TNFS_RESULT_END_OF_FILE;
    }
    // Reject if we have nothing in the cache
    if (pFHI->cache_available == 0)
    {
        Debug_print("_tnfs_read_from_cache - nothing in cache\n");
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

            Debug_printf("TNFS cache providing %u bytes\n", bytes_provided);
            memcpy(dest + (*dest_used), pFHI->cache + (pFHI->cached_pos - pFHI->cache_start), bytes_provided);

#ifdef DEBUG
            //_tnfs_cache_dump("CACHE PROVIDED", dest + (*dest_used), bytes_provided);
#endif

            pFHI->cached_pos += bytes_provided;
            *dest_used += bytes_provided;

            // Report if we've reached the end of the file
            if (pFHI->cached_pos >= pFHI->file_size)
            {
                Debug_print("_tnfs_read_from_cache - reached EOF\n");
                return TNFS_RESULT_END_OF_FILE;
            }
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
    Debug_printf("_TNFS_FILL_CACHE fh=%d, file_position=%d\n", pFHI->handle_id, pFHI->file_position);

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

        Debug_printf("_tnfs_fill_cache requesting %u bytes\n", bytes_to_read);

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

                Debug_printf("_tnfs_fill_cache got %u bytes, %u more bytes needed\n", bytes_read, bytes_remaining_to_load);
            }
            else if(tnfs_result == TNFS_RESULT_END_OF_FILE)
            {
                // Stop if we got an EOF result
                Debug_print("_tnfs_fill_cache got EOF\n");
                break;
            }
            else
            {
                Debug_printf("_tnfs_fill_cache unexepcted result: %u\n", tnfs_result);
                error = tnfs_result;
                break;
            }
        }
        else
        {
            Debug_print("_tnfs_fill_cache received failure condition on TNFS read attempt\n");
            error = -1;
            break;
        }
    }

    // If we're successful, note the total number of valid bytes in our cache
    if (error == 0)
    {
        pFHI->cache_available = sizeof(pFHI->cache) - bytes_remaining_to_load;
#ifdef DEBUG
        //_tnfs_cache_dump("CACHE FILL RESULTS", pFHI->cache, pFHI->cache_available);
#endif
    }

    return error;
}

/*
READ - Reads from a file - Command 0x21
---------------------------------------
Reads a block of data from a file. Consists of the standard header
followed by the file descriptor as returned by OPEN, then a 16 bit
little endian integer specifying the size of data that is requested.

The server will only reply with as much data as fits in the maximum
TNFS datagram size of 1K when using UDP as a transport. For the
TCP transport, sequencing and buffering etc. are just left up to
the TCP stack, so a READ operation can return blocks of up to 64K. 

If there is less than the size requested remaining in the file, 
the server will return the remainder of the file.  Subsequent READ 
commands will return the code EOF.

Examples:
Read from fd 4, maximum 256 bytes:

0xBEEF 0x00 0x21 0x04 0x00 0x01

The server will reply with the standard header, followed by the single
byte return code, the actual amount of bytes read as a 16 bit unsigned
little endian value, then the data, for example, 256 bytes:

0xBEEF 0x00 0x21 0x00 0x00 0x01 ...data...

End-of-file reached:

0xBEEF 0x00 0x21 0x21
*/
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

    Debug_printf("tnfs_read fh=%d, len=%d\n", file_handle, bufflen);

    int result = 0;
    // Try to fulfill the request using our internal cache
    while ((result = _tnfs_read_from_cache(pFileInf, buffer, bufflen, resultlen)) != 0 && result != TNFS_RESULT_END_OF_FILE)
    {
        // Reload the cache if we couldn't fulfill the request
        result = _tnfs_fill_cache(m_info, pFileInf);
        if (result != 0)
        {
            Debug_printf("tnfs_read cache fill failed (%u) - aborting", result);
            break;
        }
    }

    return result;
}


/*
WRITE - Writes to a file - Command 0x22
---------------------------------------
Writes a block of data to a file. Consists of the standard header,
followed by the file descriptor, followed by a 16 bit little endian
value containing the size of the data, followed by the data. The
entire message must fit in a single datagram.

Examples:
Write to fd 4, 256 bytes of data:

0xBEEF 0x00 0x22 0x04 0x00 0x01 ...data...

The server replies with the standard header, followed by the return
code, and the number of bytes actually written. For example:

0xBEEF 0x00 0x22 0x00 0x00 0x01 - Successful write of 256 bytes
0xBEEF 0x00 0x22 0x06 - Failed write, error is "bad file descriptor"
*/
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
            Debug_print("TNFS seek failed during write\n");
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
            // Debug_printf("tnfs_write prev_pos: %u, read: %u, new_pos: %u\n", pFileInf->file_position, *resultlen, new_pos);
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
    Debug_printf("_tnfs_cache_seek current=%u, destination=%u, cache_start=%u, cache_end=%u\n",
                 pFHI->cached_pos, destination_pos, pFHI->cache_start, cache_end);

    // Just update our position if we're within the cached region
    if (destination_pos >= pFHI->cache_start && destination_pos < cache_end)
    {
        Debug_println("_tnfs_cache_seek within cached region");
        pFHI->cached_pos = destination_pos;
        return 0;
    }

    Debug_println("_tnfs_cache_seek outside cached region");
    return -1;
}

/*
LSEEK - Seeks to a new position in a file - Command 0x25
--------------------------------------------------------
Seeks to an absolute position in a file, or a relative offset in a file,
or to the end of a file.
The request consists of the header, followed by the file descriptor,
followed by the seek type (SEEK_SET, SEEK_CUR or SEEK_END), followed
by the position to seek to. The seek position is a signed 32 bit integer,
little endian. (2GB file sizes should be more than enough for 8 bit
systems!)

The seek types are defined as follows:
0x00		SEEK_SET - Go to an absolute position in the file
0x01		SEEK_CUR - Go to a relative offset from the current position
0x02		SEEK_END - Seek to EOF

Example:

File descriptor is 4, type is SEEK_SET, and position is 0xDEADBEEF:
0xBEEF 0x00 0x25 0x04 0x00 0xEF 0xBE 0xAD 0xDE

Note that clients that buffer reads for single-byte reads will have
to make a calculation to implement SEEK_CUR correctly since the server's
file pointer will be wherever the last read block made it end up.
*/
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

    Debug_printf("tnfs_lseek currpos=%d, pos=%d, typ=%d\n", pFileInf->cached_pos, position, type);

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
            Debug_printf("tnfs_lseek success, new pos=%u, response pos=%u\n", pFileInf->file_position, response_pos);

            // TODO: This is temporary while we confirm that the recently-changed TNFSD code matches what we've been doing prior
            if(pFileInf->file_position != response_pos)
            {
                Debug_print("CALCULATED AND RESPONSE POS DON'T MATCH!\n");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
            }
        }
        return packet.payload[0];
    }
    return -1;
}

/*
OPENDIR - Open a directory for reading - Command ID 0x10
--------------------------------------------------------

Format:
Standard header followed by a null terminated absolute path.
The path delimiter is always a "/". Servers whose underlying 
file system uses other delimiters, such as Acorn ADFS, should 
translate. Note that any recent version of Windows understands "/" 
to be a path delimiter, so a Windows server does not need
to translate a "/" to a "\".
Clients should keep track of their own current working directory.

Example:
0xBEEF 0x00 0x10 /home/tnfs 0x00 - Open absolute path "/home/tnfs"

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, and if successful, byte 5 
is set to the directory handle.

Example:
0xBEEF 0x00 0x10 0x00 0x04 - Successful, handle is 0x04
0xBEEF 0x00 0x10 0x1F - Failed with code 0x1F
*/
/*
    Opens directory and stores directory handle in tnfsMountInfo.dir_handle
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
/*
int tnfs_opendir(tnfsMountInfo *m_info, const char *directory)
{
    if (m_info == nullptr || directory == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_OPENDIR;
    int len = _tnfs_adjust_with_full_path(m_info, (char *)packet.payload, directory, sizeof(packet.payload));

    Debug_printf("TNFS open directory: \"%s\"\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            m_info->dir_handle = packet.payload[1];
            // Debug_printf("Directory opened, handle ID: %hhd\n", m_info->dir_handle);
        }
        return packet.payload[0];
    }
    return -1;
}
*/
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

    tnfsPacket packet;
    packet.command = TNFS_CMD_OPENDIRX;

    packet.payload[OFFSET_OPENDIRX_DIROPT] = diropts;
    packet.payload[OFFSET_OPENDIRX_SORTOPT] =  sortopts;

    packet.payload[OFFSET_OPENDIRX_MAXRESULTS] = TNFS_LOBYTE_FROM_UINT16(maxresults);
    packet.payload[OFFSET_OPENDIRX_MAXRESULTS + 1] = TNFS_HIBYTE_FROM_UINT16(maxresults);

    // Copy the pattern or an empty string
    strncpy((char *)(packet.payload + OFFSET_OPENDIRX_PATTERN),
        pattern == nullptr ? "" : pattern,
        sizeof(packet.payload) - OPENDIRX_HEADERBYTES - 1);

    // Calculate the new offset to the path taking the pattern string into account
    int pathoffset = strlen((char *)(packet.payload + OFFSET_OPENDIRX_PATTERN)) + OPENDIRX_HEADERBYTES + 1;

    // Copy the directory into the right spot in the packet and get its string len
    int pathlen = _tnfs_adjust_with_full_path(m_info, 
        (char *)(packet.payload + pathoffset), directory, sizeof(packet.payload) - pathoffset);

    Debug_printf("TNFS open directory: sortopts=0x%02x diropts=0x%02x maxresults=0x%04x pattern=\"%s\" path=\"%s\"\n",
     sortopts, diropts, maxresults, (char *)(packet.payload + OFFSET_OPENDIRX_PATTERN), (char *)(packet.payload + pathoffset));

    if (_tnfs_transaction(m_info, packet, pathoffset + pathlen + 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            m_info->dir_handle = packet.payload[1];
            m_info->dir_entries = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + 2);
            Debug_printf("Directory opened, handle ID: %hhd, entries: %u\n", m_info->dir_handle, m_info->dir_entries);
        }
        return packet.payload[0];
    }
    return -1;
}

/*
READDIR - Reads a directory entry - Command ID 0x11
---------------------------------------------------

Format:
Standard header plus directory handle.

Example:
0xBEEF 0x00 0x11 0x04 - Read an entry with directory handle 0x04

The server responds with the standard header, followed by the directory
entry. Example:

0xBEEF 0x17 0x11 0x00 . 0x00 - Directory entry for the current working directory
0xBEEF 0x18 0x11 0x00 .. 0x00 - Directory entry for parent
0xBEEF 0x19 0x11 0x00 foo 0x00 - File named "foo"

If the end of directory is reached, or another error occurs, then the
status byte is set to the error number as for other commands.
0xBEEF 0x1A 0x11 0x21 - EOF
0xBEEF 0x1B 0x11 0x1F - Error code 0x1F

*/
/*
    Reads next available file using open directory handle specified in
    tnfsMountInfo.dir_handle
    dir_entry filled with filename up to dir_entry_len
 returns: 0: success, -1: failed to deliver/receive packet, other: TNFS error result code
*/
/*
int tnfs_readdir(tnfsMountInfo *m_info, char *dir_entry, int dir_entry_len)
{
    // Check for a valid open handle ID
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_READDIR;
    packet.payload[0] = m_info->dir_handle;

    if (_tnfs_transaction(m_info, packet, 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            strncpy(dir_entry, (char *)&packet.payload[1], dir_entry_len);
        }
        return packet.payload[0];
    }
    return -1;
}
*/

int tnfs_readdirx(tnfsMountInfo *m_info, tnfsStat *filestat, char *dir_entry, int dir_entry_len)
{
    // Check for a valid open handle ID
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

#define OFFSET_READDIRX_FLAGS 1
#define OFFSET_READDIRX_SIZE 2
#define OFFSET_READDIRX_MTIME 6
#define OFFSET_READDIRX_CTIME 10
#define OFFSET_READDIRX_PATH 14

    tnfsPacket packet;
    packet.command = TNFS_CMD_READDIRX;
    packet.payload[0] = m_info->dir_handle;
    // Number of responses to read
    packet.payload[1] = 1;

    if (_tnfs_transaction(m_info, packet, 2))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            uint8_t response_count = packet.payload[1];
            uint16_t dirpos = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + 2 );

            Debug_printf("tnfs_readdirx resp_count=%hu, dirpos=%hu\n", response_count, dirpos);

            int offset = 3;

            filestat->isDir = (packet.payload[offset + OFFSET_READDIRX_FLAGS] & TNFS_READDIRX_DIR) ? true : false;
            filestat->filesize = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + offset + OFFSET_READDIRX_SIZE);
            filestat->m_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + offset + OFFSET_READDIRX_MTIME);
            filestat->c_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + offset + OFFSET_READDIRX_CTIME);
            filestat->a_time = 0;

            strncpy(dir_entry, (char *)packet.payload + offset + OFFSET_READDIRX_PATH, dir_entry_len);

#ifdef DEBUG
            char t_m[80];
            char t_c[80];
            const char *tfmt ="%Y-%m-%d %H:%M:%S";
            time_t tt = filestat->m_time;
            strftime(t_m, sizeof(t_m), tfmt, localtime(&tt));
            tt = filestat->c_time;
            strftime(t_c, sizeof(t_c), tfmt, localtime(&tt));
            Debug_printf("\ttnfs_readdirx: dir: %s, size: %u, mtime: %s, ctime: %s \"%s\"\n", 
                filestat->isDir ? "Yes" : "no",
                 filestat->filesize, t_m, t_c, dir_entry );
#endif

        }
        return packet.payload[0];
    }
    return -1;
}

/*
    TELLDIR
*/
int tnfs_telldir(tnfsMountInfo *m_info, uint32_t *position)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    if(position == nullptr)
        return -1;

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
int tnfs_seekdir(tnfsMountInfo *m_info, uint32_t position)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_SEEKDIR;
    packet.payload[0] = m_info->dir_handle;
    TNFS_UINT32_TO_LOHI_BYTEPTR(position, packet.payload + 1);

    if (_tnfs_transaction(m_info, packet, 5))
        return packet.payload[0];

    return -1;
}

/*
CLOSEDIR - Close a directory handle - Command ID 0x12
-----------------------------------------------------

Format:
Standard header plus directory handle.

Example, closing handle 0x04:
0xBEEF 0x00 0x12 0x04

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, or something else for an error.
Example:
0xBEEF 0x00 0x12 0x00 - Close operation succeeded.
0xBEEF 0x00 0x12 0x1F - Close failed with error code 0x1F
*/
/*
    Closes current directory handle specificed in tnfsMountInfo
    Returns: 0: success, -1: failed to send/receive packet, other: TNFS server response
*/
int tnfs_closedir(tnfsMountInfo *m_info)
{
    if (m_info == nullptr || false == TNFS_VALID_AS_UINT8(m_info->dir_handle))
        return -1;

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
MKDIR - Make a new directory - Command ID 0x13
----------------------------------------------

Format:
Standard header plus a null-terminated absolute path.

Example:
0xBEEF 0x00 0x13 /foo/bar/baz 0x00

The server responds with the standard header plus the return code:
0xBEEF 0x00 0x13 0x00 - Directory created successfully
0xBEEF 0x00 0x13 0x02 - Directory creation failed with error 0x02
*/
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

    Debug_printf("TNFS make directory: \"%s\"\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
RMDIR - Remove a directory - Command ID 0x14
--------------------------------------------

Format:
Standard header plus a null-terminated absolute path.

Example:
0xBEEF 0x00 0x14 /foo/bar/baz 0x00

The server responds with the standard header plus the return code:
0xBEEF 0x00 0x14 0x00 - Directory was deleted.
0xBEEF 0x00 0x14 0x02 - Directory delete operation failed with error 0x02
*/
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

    Debug_printf("TNFS remove directory: \"%s\"\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
STAT - Get information on a file - Command 0x24
-----------------------------------------------
The request consists of the standard header, followed by the full path
of the file to stat, terminated by a NULL. Example:

0xBEEF 0x00 0x24 /foo/bar/baz.txt 0x00

The server replies with the standard header, followed by the return code.
On success, the file information follows this. Stat information is returned
in this order. Not all values are used by all servers. At least file
mode and size must be set to a valid value (many programs depend on these).

File mode	- 2 bytes: file permissions - little endian byte order
uid		- 2 bytes: Numeric UID of owner
gid		- 2 bytes: Numeric GID of owner
size		- 4 bytes: Unsigned 32 bit little endian size of file in bytes
atime		- 4 bytes: Access time in seconds since the epoch, little end.
mtime		- 4 bytes: Modification time in seconds since the epoch,
                           little endian
ctime		- 4 bytes: Time of last status change, as above.
uidstring	- 0 or more bytes: Null terminated user id string
gidstring	- 0 or more bytes: Null terminated group id string

Fields that don't apply to the server in question should be left as 0x00.
The ´mtime' field and 'size' fields are unsigned 32 bit integers.
The uidstring and gidstring are helper fields so the client doesn't have
to then ask the server for the string representing the uid and gid.

File mode flags will be most useful for code that is showing a directory
listing, and for programs that need to find out what kind of file (regular
file or directory, etc) a particular file may be. They follow the POSIX
convention which is:

Flags		Octal representation
S_IFREG		0100000		Is a regular file
S_IFDIR		0040000		Directory
*/
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

    // Debug_printf("TNFS stat: \"%s\"\n", (char *)packet.payload);

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

            uint16_t filemode = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_FILEMODE);
            filestat->isDir = (filemode & S_IFDIR) ? true : false;

            uint16_t uid = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_UID);
            uint16_t gid = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_GID);

            filestat->filesize = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_FILESIZE);

            filestat->a_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_ATIME);
            filestat->m_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_MTIME);
            filestat->c_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_STAT_CTIME);

            /*
            Debug_printf("\ttnfs_stat: mode: %ho, uid: %hu, gid: %hu, dir: %d, size: %u, atime: 0x%04x, mtime: 0x%04x, ctime: 0x%04x\n", 
                filemode, uid, gid,
                filestat->isDir ? 1 : 0, filestat->filesize, filestat->a_time, filestat->m_time, filestat->c_time );
            */
        }
        __END_IGNORE_UNUSEDVARS
        return packet.payload[0];
    }
    return -1;
}

/*
UNLINK - Unlinks (deletes) a file - Command 0x26
------------------------------------------------
Removes the specified file. The request consists of the header then
the null terminated full path to the file. The reply consists of the
header and the return code.

Example:
Unlink file "/foo/bar/baz.bas"
0xBEEF 0x00 0x26 /foo/bar/baz.bas 0x00
*/
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

    Debug_printf("TNFS unlink file: \"%s\"\n", (char *)packet.payload);

    if (_tnfs_transaction(m_info, packet, len + 1))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
RENAME - Moves a file within a filesystem - Command 0x28
--------------------------------------------------------
Renames a file (or moves a file within a filesystem - it must be possible
to move a file to a different directory within the same FS on the
server using this command).
The request consists of the header, followed by the null terminated
source path, and the null terminated destination path.

Example: Move file "foo.txt" to "bar.txt"
0xBEEF 0x00 0x28 foo.txt 0x00 bar.txt 0x00
*/
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

    Debug_printf("TNFS rename file: \"%s\" -> \"%s\"\n", (char *)packet.payload, (char *)(packet.payload + l1));

    if (_tnfs_transaction(m_info, packet, l1 + l2))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
CHMOD - Changes permissions on a file - Command 0x27
----------------------------------------------------
Changes file permissions on the specified file, using POSIX permissions
semantics. Not all permissions may be supported by all servers - most 8
bit systems, for example, may only support removing the write bit.
A server running on something Unixish will support everything.

The request consists of the header, followed by the 16 bit file mode,
followed by the null terminated filename. Filemode is sent as a little
endian value. See the Unix manpage for chmod(2) for further information.

File modes are as defined by POSIX. The POSIX definitions are as follows:
              
Flag      Octal Description
S_ISUID   04000 set user ID on execution
S_ISGID   02000 set group ID on execution
S_ISVTX   01000 sticky bit
S_IRUSR   00400 read by owner
S_IWUSR   00200 write by owner
S_IXUSR   00100 execute/search by owner
S_IRGRP   00040 read by group
S_IWGRP   00020 write by group
S_IXGRP   00010 execute/search by group
S_IROTH   00004 read by others
S_IWOTH   00002 write by others
S_IXOTH   00001 execute/search by others

Example: Set permissions to 755 on /foo/bar/baz.bas:
0xBEEF 0x00 0x27 0xED 0x01 /foo/bar/baz.bas

The reply is the standard header plus the return code of the chmod operation.
*/
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

    Debug_printf("TNFS chmod file: \"%s\", %ho\n", (char *)packet.payload + 2, mode);

    if (_tnfs_transaction(m_info, packet, len + 3))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
SIZE - Requests the size of the mounted filesystem - Command 0x30
-----------------------------------------------------------------
Finds the size, in kilobytes, of the filesystem that is currently mounted.
The request consists of a standard header and nothing more.

Example:
0xBEEF 0x00 0x30

The reply is the standard header, followed by the return code, followed
by a 32 bit little endian integer which is the size of the filesystem
in kilobytes, for example:

0xBEEF 0x00 0x30 0x00 0xD0 0x02 0x00 0x00 - Filesystem is 720kbytes
0xBEEF 0x00 0x30 0xFF - Request failed with error code 0xFF
*/
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

/*
FREE - Requests the amount of free space on the filesystem - Command 0x31
-------------------------------------------------------------------------
Finds the size, in kilobytes, of the free space remaining on the mounted
filesystem. The request consists of the standard header and nothing more.

Example:
0xBEEF 0x00 0x31

The reply is as for SIZE - the standard header, return code, and little
endian integer for the free space in kilobytes, for example:

0xBEEF 0x00 0x31 0x00 0x64 0x00 0x00 0x00 - There is 64K free.
0xBEEF 0x00 0x31 0x1F - Request failed with error 0x1F
*/
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
    tnfsFileHandleInfo *pFileInf = m_info->get_filehandleinfo(file_handle);
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
    fnUDP udp;

    // Set our session ID
    pkt.session_idl = TNFS_LOBYTE_FROM_UINT16(m_info->session);
    pkt.session_idh = TNFS_HIBYTE_FROM_UINT16(m_info->session);

    // Start a new retry sequence
    int retry = 0;
    while (retry < m_info->max_retries)
    {
        // Set the sequence number
        pkt.sequence_num = m_info->current_sequence_num++;

#ifdef DEBUG
        _tnfs_debug_packet(pkt, payload_size);
#endif

        // Send packet
        bool sent = false;
        // Use the IP address if we have it
        if (m_info->host_ip != IPADDR_NONE)
            sent = udp.beginPacket(m_info->host_ip, m_info->port);
        else
            sent = udp.beginPacket(m_info->hostname, m_info->port);

        if (sent)
        {
            udp.write(pkt.rawData, payload_size + TNFS_HEADER_SIZE); // Add the data payload along with 4 bytes of TNFS header
            sent = udp.endPacket();
        }

        if (!sent)
        {
            Debug_println("Failed to send packet - retrying");
        }
        else
        {
            // Wait for a response at most TNFS_TIMEOUT milliseconds
            int ms_start = fnSystem.millis();
            uint8_t current_sequence_num = pkt.sequence_num;
            do
            {
                if (udp.parsePacket())
                {
                    unsigned short l = udp.read(pkt.rawData, sizeof(pkt.rawData));
                    __IGNORE_UNUSED_VAR(l);
#ifdef DEBUG
                    _tnfs_debug_packet(pkt, l, true);
#endif

                    // Out of order packet received.
                    if (pkt.sequence_num != current_sequence_num)
                    {
                        Debug_println("TNFS OUT OF ORDER SEQUENCE! RETRYING");
                        // Fall through and let retry logic handle it.
                    }
                    else
                    {
                        // Check in case the server asks us to wait and try again
                        if (pkt.payload[0] != TNFS_RESULT_TRY_AGAIN)
                            return true;
                        else
                        {
                            // Server should tell us how long it wants us to wait
                            uint16_t backoffms = TNFS_UINT16_FROM_LOHI_BYTEPTR(pkt.payload + 1);
                            Debug_printf("Server asked us to TRY AGAIN after %ums\n", backoffms);
                            if (backoffms > TNFS_MAX_BACKOFF_DELAY)
                                backoffms = TNFS_MAX_BACKOFF_DELAY;
                            vTaskDelay(backoffms / portTICK_PERIOD_MS);
                        }
                    }
                }
                fnSystem.yield();

            } while ((fnSystem.millis() - ms_start) < m_info->timeout_ms);

            Debug_printf("Timeout after %d milliseconds. Retrying\n", m_info->timeout_ms);
        }

        // Make sure we wait before retrying
        vTaskDelay(m_info->min_retry_ms / portTICK_PERIOD_MS);
        retry++;
    }

    Debug_println("Retry attempts failed");

    return false;
}

// Copies to buffer while ensuring that we start with a '/'
// Returns length of new full path or -1 on failure
int _tnfs_adjust_with_full_path(tnfsMountInfo *m_info, char *buffer, const char *source, int bufflen)
{
    if (buffer == nullptr || bufflen < 2)
        return -1;

    // Use the cwd to bulid the full path
    strncpy(buffer, m_info->current_working_directory, bufflen);

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
    strncpy(buffer + ll, source, bufflen - ll);

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
#ifdef TNFS_DEBUG_VERBOSE
    // Remove header bytes from count of response packets since we only care about the count of the data payload
    if (isResponse)
    {
        payload_size -= TNFS_HEADER_SIZE;
        Debug_printf("TNFS << RX cmd: %s, len: %d, response (%hhu): %s\n", _tnfs_command_string(pkt.command), payload_size, pkt.payload[0], _tnfs_result_code_string(pkt.payload[0]));
    }
    else
        Debug_printf("TNFS >> TX cmd: %s, len: %d\n", _tnfs_command_string(pkt.command), payload_size);

    Debug_printf("\t[%02x%02x %02x %02x] ", pkt.session_idh, pkt.session_idl, pkt.sequence_num, pkt.command);
    for (int i = 0; i < payload_size; i++)
        Debug_printf("%02x ", pkt.payload[i]);
    Debug_println();
#endif
}

const char *_tnfs_command_string(int command)
{
#ifdef TNFS_DEBUG_VERBOSE
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
#ifdef TNFS_DEBUG_VERBOSE
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
