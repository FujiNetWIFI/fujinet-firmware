The TNFS Protocol
===========================
Updated July 15, 2020

### Rationale

Protocols such as NFS (Unix) or SMB (Windows) are overly complex for 8 bit
systems. While NFS is well documented, it's a complex RPC based protocol.
SMB is much worse. It is also a complex RPC based protocol, but it's also
proprietary, poorly documented, and implementations differ so much that
to get something that works with a reasonable subset of SMB would add a
great deal of unwanted complexity. The Samba project has been going for 
/years/ and they still haven't finished making it bug-for-bug compatible with
the various versions of Windows!

At the other end, there's FTP, but FTP is not great for a general file
system protocol for 8 bit systems - it requires two TCP sockets for
each connection, and some things are awkward in FTP, even if they work.

So instead, TNFS provides a straightforward protocol that can easily be
implemented on most 8 bit systems. It's designed to be a bit better than
FTP for the purpose of a filesystem, but not as complex as the "big" network
filesystem protocols like NFS or SMB.

For a PC, TNFS can be implemented using something like FUSE (no, not the
Spectrum emulator, but the Filesystem In Userspace project). This is
at least available for most things Unixy (Linux, OS X, some of the BSDs),
and possibly by now for Windows also.

### Security

This is not intended to be a proper, secure network file system. If you're
storing confidential files on your Speccy, you're barmy :)  Encryption,
for example, is not supported. However, servers that may be exposed to the 
internet should be coded in such a way they won't open up the host system 
to exploits.


Operations Supported
====================
These generally follow the POSIX equivalents. Entries ending in "*" are mandatory
for servers to support.

Sessions
--------
* MOUNT - Connect to a TNFS filesystem *
* UMOUNT - Disconnect from a TNFS filesystem *

Directories
-----------
* OPENDIR - Opens a directory for reading *
* OPENDIRX - Opens a directory for reading with additional options
* READDIR - Reads a directory entry *
* READDIRX - Reads a directory entry and returns extended results
* TELLDIR - Returns position of current reddir for use with seekdir
* SEEKDIR - Sets position of next readdir as provided by telldir
* CLOSEDIR - Closes the directory *
* RMDIR - Removes a directory
* MKDIR - Creates a directory

Files
-----
* OPEN - Opens a file *
* READ - Reads from an open file *
* WRITE - Writes to an open file
* CLOSE - Closes a file *
* STAT - Gets information about a file *
* LSEEK - Set the position in the file where the next byte will be read/written
* CHMOD - Change file access
* UNLINK - Remove a file

Devices
-------
* SIZE - Get the size of the filesystem *
* FREE - Get the remaining free space on the filesystem *

Note: Not all servers have to support all operations - for example, a server
on a Spectrum with a microdrive, or +3 floppy won't support
mkdir/rmdir and will only support limited options for chmod. But
a BBC Micro with ADFS would support mkdir/rmdir and more file mode options.

The directory delimiter in all cases is a "/". A server running on a filesystem
that has a different delimiter will have to translate, for example,
on a BBC with ADFS, the / would need to be translated to a "." for the
underlying OS operation. 


Protocol "On the Wire"
======================

The lowest common denominator is TNFS over UDP. UDP is the 'mandatory'
one because it demands the least of possibly small TCP/IP stacks which may
have limited resources for TCP sockets. All TNFS servers must
support the protocol on UDP port 16384. TCP is optional.

Each datagram has a header. The header is formatted the same way for all
datagrams:


    Bytes 0,1  Connection ID (ignored for client's "mount" command)
    Byte  2	   Retry number
    Byte  3	   Command

The connection ID is to add extra identifying information, since the same
machine can establish more than one connection to the same server and may
do so with different credentials.

Byte 2 is a sequence number. This allows the receiver to determine whether
the datagram it just got is a retry or not. It should be incremented
by one for each request sent. Clients should discard datagrams from the
server if the sequence number does not match the number that was in
the request datagram.

The last byte is the command.

The remaining data in the datagram are specific to each command. However,
in any command that may return more than one status (i.e. a command that
can be either succeed or fail in one or more way), byte 4 is the
status of the command, and further data follows from byte 5.

Every command should yield exactly one datagram in response. A high
level operation (such as a call to read()) asking for a buffer larger
than the size of one UDP datagram should manage this with as many requests
and responses as is necessary to fill the buffer.

The server can also ask the client to back off. If a server can operate
with interrupts enabled while the physical disc is busy, and therefore
still be able to process requests, it can tell the client that it is busy
and to try again later. In this case, the `EAGAIN` error code will be
returned for whatever command was being tried, and following the error
code, will be a 16 bit little endian value giving how long to back off in
milliseconds. Servers that have this ability should use it, as the server
can then better control contention on a slow device, like a floppy disk,
since the server can figure out how many requests clients are trying
to make and set the back-off value accordingly. Clients should retry as normal
once the back-off time expires.

As can be seen from this very simple wire protocol, TNFS is not designed
for confidentiality or security. You have been warned.


*TNFS Commands Datagrams*
=========================


Session Operations  
==================
This is how you log on and log off a TNFS server.


MOUNT
---------------------------------------------------------------------------
> _Establish a new session_   
> Command `0x00`

Standard header followed by:

    Bytes 4+: 16 bit version number, little endian (LSB = minor, MSB = major)
    NULL terminated string: mount location
    NULL terminated string: user id (optional - NULL if no user id)
    NULL terminated string: password (optional - NULL if no passwd)

Example:

To mount `/home/tnfs` on the server, with user id `example` and password of
`password`, using version 1.2 of the protocol:

    0x0000 0x00 0x00 0x02 0x01 /home/tnfs 0x00 example 0x00 password 0x00

To mount `A:` anonymously, using version 1.2 of the protocol:

    0x0000 0x00 0x00 0x02 0x01 A: 0x00 0x00 0x00

The server responds with the standard header. If the operation was successful,
the standard header contains the session number, and the TNFS protocol
version that the server is using following the header, followed by the
minimum retry time in milliseconds as a little-endian 16 bit number.
Clients must respect this minimum retry value, especially for a server
with a slow underlying file system such as a floppy disk, to avoid swamping
the server. A client should also never have more than one request "in flight"
at any one time for any operation where order is important, so for example,
if reading a file, don't send a new request to read from a given file handle
before completing the last request.

Example:

A successful MOUNT command was carried out, with a server that
supports version 2.6, and has a minimum retry time of 5 seconds (5000 ms,
hex 0x1388). Session ID is 0xBEEF:

    0xBEEF 0x00 0x00 0x00 0x06 0x02 0x88 0x13

Example:

A failed MOUNT command with error 1F for a version 3.5 server:

    0x0000 0x00 0x00 0x1F 0x05 0x03


UMOUNT
---------------------------------------------------------------------------
> _Close an established session_   
> Command `0x01`

Standard header only, containing the connection ID to terminate, 0x00 as
the sequence number, and 0x01 as the command.

Example:

To UMOUNT the filesystem mounted with id 0xBEEF:

    0xBEEF 0x00 0x01

The server responds with the standard header and a return code as byte 4.
The return code is 0x00 for OK.

Example:

    0xBEEF 0x00 0x01 0x00

On error, byte 4 is set to the error code, for example, for error 0x1F:

    0xBEEF 0x00 0x01 0x1F


Directory Operations  
====================
Don't confuse this with the ability of having a directory heirachy. Even
servers (such as a +3 with a floppy) that don't have heirachical filesystems
must support cataloguing a disc, and cataloguing a disc requires opening,
reading, and closing the catalogue. It's the only way to do it!


OPENDIR
---------------------------------------------------------------------------
> _Open a directory for reading_   
> Command `0x10`

Standard header followed by a null terminated absolute path.
The path delimiter is always a "/". Servers whose underlying 
file system uses other delimiters, such as Acorn ADFS, should 
translate. Note that any recent version of Windows understands "/" 
to be a path delimiter, so a Windows server does not need
to translate a "/" to a "\\".

Clients should keep track of their own current working directory.

Example:

Open absolute path `/home/tnfs`:

    0xBEEF 0x00 0x10 /home/tnfs 0x00

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, and if successful, byte 5 
is set to the directory handle.

Example:

Successful, handle is 0x04:

    0xBEEF 0x00 0x10 0x00 0x04
 
 Failed with code 0x1F:
 
    0xBEEF 0x00 0x10 0x1F


OPENDIRX
----------------------------------------------------------------------------
> _Open a directory for reading with additional options_  
> By default, OPENDIRX sorts the directory results (see below for details).
> You're also given the option to provide a wildcard string to filter
> the results returned, taking the burden of that work off of the client and
> potentially reducing the amount round trips needed to provide a directory
> listing. This is particularly important when the server is not on the same
> network as the client. Although the results can be read using the standard
> READDIR command, see the complementary READDIRX command for a more
> feature-rich alternative.  
> Command `0x17`

Standard header followed by:

    1 byte   - directory options TNFS_DIROPT (see below)
    1 byte   - sorting options TNFS_DIRSORT (see below)
    2 bytes  - max results to return or 0 for unlimited (16-bit unsigned little-endian)
    1+ bytes - zero-terminated wildcard pattern (no pattern used if empty)
    2+ bytes - zero-terminated absolute directory path

The path delimiter is always a "/". Servers whose underlying 
file system uses other delimiters, such as Acorn ADFS, should 
translate. Note that any recent version of Windows understands "/" 
to be a path delimiter, so a Windows server does not need
to translate a "/" to a "\\".

Example:

Use the default TNFS_DIROPT options, `TNFS_DIROSRT_NONE` sorting option, 16 results
maximum, the wildcard pattern `*.ATR`, and the absolute directory path `/home/tnfs`

    0xBEEF 0x00 0x17 0x00 0x01 0x1000 *.ATR 0x00 /home/tnfs 0x00

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, and if successful, byte 5 
is set to the directory handle. Bytes 6-7 are an unsigned 16-bit little-endian
value containing the number of matching directory entries found.

Example:

 Successful, handle is 0x04, 790 entries found:

    0xBEEF 0x00 0x10 0x00 0x04 0x16030000
 
 Failed with code 0x1F:

    0xBEEF 0x00 0x10 0x1F

The default searching and sorting behavior is:
* Do not show hidden files
* Do not show special files (e.g. "." and ".." on Windows)
* Sort by name in ascending order (A-Z)
* Directories sorted before regular files
* Case-insensitve sort order
* Wildcard pattern does not apply to directories

The following flags are provided to override these defaults:

`TNFS_DIROPT` Options:

* TNFS_DIROPT_NO_FOLDERSFIRST - Disable sorting directories before reuglar files
* TNFS_DIROPT_NO_SKIPHIDDEN - Disable ignoring hidden files
* TNFS_DIROPT_NO_SKIPSPECIAL - Disable ignoring special flies
* TNFS_DIROPT_DIR_PATTERN - Match pattern applies to directories also

`TNFS_DIRSORT` Options:

* TNFS_DIRSORT_NONE - Disable all sorting
* TNFS_DIRSORT_CASE - Sorting is case-sensitive
* TNFS_DIRSORT_DESCENDING - Sorting is descendnig, not ascending
* TNFS_DIRSORT_MODIFIED - Sort by file's modified timestamp instead of name
* TNFS_DIRSORT_SIZE - Sort by the file's size instead of name


READDIR
---------------------------------------------------------------------------
> _Reads a directory entry_   
> Command `0x11`

Standard header plus directory handle.

Example:

 Read an entry with directory handle 0x04:

    0xBEEF 0x00 0x11 0x04

The server responds with the standard header, followed by the directory
entry. Example:

    0xBEEF 0x17 0x11 0x00 . 0x00
    0xBEEF 0x18 0x11 0x00 .. 0x00
    0xBEEF 0x19 0x11 0x00 foo 0x00

If the end of directory is reached, or another error occurs, then the
status byte is set to the error number as for other commands.

    0xBEEF 0x1A 0x11 0x21 - EOF
    0xBEEF 0x1B 0x11 0x1F - Error code 0x1F


READDIRX
---------------------------------------------------------------------------
> _Reads a directory entry while providing additional information_  
> In additional to returning a standard directory entry as `READDIR` does,
> `READDIRX` includes basic `STAT` information on each entry. This reduces
> the number of round trips that are needed to load directories when more
> than just the file/directory name is required. The `TELLDIR` position is
> also returned with every response to aid clients deal with large directories. 
> `READDIRX` can optionally return multiple entries in each response to
> improve throughput even further. Note that `OPENDIRX` must be used to
> initiate the directory read, otherwise `READDIRX` may not return any results.  
> Command `0x18`

Standard header followed by directory handle (opened by `OPENDIRX`) and a single
byte indicating the number of entries desired. A zero for this value will
result in the server providing as many entries as fit within a single response
(when using UDP). Any other value will cause the server to return no more
than that many entries, although less than that may be provided if that
number will not fit in a single response or if there are less than that many
entries remaining.

When the final available directory entires are returned, the `status` byte
will indicate the end of the directory by setting the `TNFS_DIRSTATUS_EOF`
flag. This tells the client the end fo the directory has been reached
without needing another round trip. Subsequent `READDIRX` attempts will return
a standard `EOF` status.

Example:

Read at most two directory entries with the open directory handle of 0x04:

    0xBEEF 0x00 0x18 0x04 0x02

The server replies with the standard header followed by the return code.
On success, the file information follows this in the following order:

Bytes | Item   | Description
:---: | ------ | -------------------------------------------------------
  1   | count  | Number of entries returned
  1   | status | `TNFS_DIRSTATUS` flags
  2   | dirpos | Position of first entry as given by `TELLDIR`
  1   | flags  | `TNFS_DIRENTRY` flags providing additional information (see below)
  4   | size   | Entry size in bytes as unsigned 32-bit little endian value
  4   | mtime  | Entry modification time in seconds since epoch
  4   | ctime  | Entry change time (as above)
  1+  | name   | Entry name as NULL-terminated string
  .   | ...    | ... additional entries (if available) ...

If more than one entry is returned in a reply (as indicated by `count`),
those entries follow after the NULL terminating the `name` of the initial
entry. Each entry begins with its own `flags` value and is followed by
`size`, `mtime`, `ctime`, and `name`.

`dirpos` may be used in subsequent calls to `SEEKDIR`. If more than one
entry is returned in the reply, the `dirpos` value for those entries is
simply calculated by incrementing the initial value by one for each entry.

`TNFS_DIRENTRY` flags:

* TNFS_DIRENTRY_DIR  - Entry denotes a directory
* TNFS_DIRENTRY_HIDDEN - Entry is hidden
* TNFS_DIRENTRY_SPECIAL - Entry is special (as defined by host OS)

`TNFS_DIRSTATUS` flags:

* TNFS_DIRSTATUS_EOF - The end of the directory has been reached

Example:

Two directory entries are returned, the first of which has a `dirpos`
of 23 (the second is assumed to be 24). The first entry is a directory
named `folder` and the second is a file named `readme.txt`. Modified time
for each is `July 15, 2020` and changed/created time for each is
`July 4, 2020`.  The size of the file is `12,316` bytes.

    0xBEEF 0x01 0x18 0x00 0x02 0x1700
           0x01 0x00000000 0x10550E5F 0x90D4FF5E folder 0x00
           0x00 0x00001C30 0x10550E5F 0x90D4FF5E readme.txt 0x00

If the end of directory is reached or another error occurs, then the
status byte is set to the error number as for other commands.

Example:

    0xBEEF 0x1A 0x18 0x21 - EOF
    0xBEEF 0x1B 0x18 0x1F - Error code 0x1F


TELLDIR
---------------------------------------------------------------------------
> _Returns position within current directory results_   
> Command `0x15`

Standard header plus directory handle.

Example:

Request position of directory with handle 6:

    0xBEEF 0x00 0x15 0x06

The server responds with the standard header plus the return code and the
directory position as a 4-bytes unsigned 32 bit little endian value.

Example:

    0xBEEF 0x00 0x15 0x00 0x01 0x00 0x00 0x00 - Directory position is 1
    0xBEEF 0x00 0x15 0x00 0x01 0x00 0x02 0x00 - Directory position is 513


SEEKDIR
---------------------------------------------------------------------------
> _Moves current directory results position to new value_  
> The position given should be one aquired by `TELLDIR`.   
> Command `0x16`

Standard header plus directory handle and unsigned 32-bit little endian
position (4 bytes).

Example:

Set position of directory stream with handle 6 to 513:

    0xBEEF 0x00 0x16 0x06 0x01 0x00 0x02 0x00

The server responds with the standard header plus the return code.

Example of success:

    0xBEEF 0x00 0x16 0x00


CLOSEDIR
---------------------------------------------------------------------------
> _Close a directory handle_   
> A handle previously opened by `OPENDIR` or `OPENDIRX` should be provided.  
> Command `0x12`

Standard header plus directory handle.

Example, closing handle 0x04:

    0xBEEF 0x00 0x12 0x04

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, or something else for an error.

Example:

Close operation succeeded:

    0xBEEF 0x00 0x12 0x00

Close failed with error code 0x1F

    0xBEEF 0x00 0x12 0x1F


MKDIR
---------------------------------------------------------------------------
> _Make a new directory_   
> Command `0x13`

Standard header plus a null-terminated absolute path.

Example:

    0xBEEF 0x00 0x13 /foo/bar/baz 0x00

The server responds with the standard header plus the return code.

Directory created successfully:

    0xBEEF 0x00 0x13 0x00

Directory creation failed with error 0x02:

    0xBEEF 0x00 0x13 0x02

RMDIR
---------------------------------------------------------------------------
> _Remove a directory_   
> Command `0x14`

Standard header plus a null-terminated absolute path.

Example:

    0xBEEF 0x00 0x14 /foo/bar/baz 0x00

The server responds with the standard header plus the return code.

Directory was deleted:

    0xBEEF 0x00 0x14 0x00

Directory delete operation failed with error 0x02:

    0xBEEF 0x00 0x14 0x02


File Operations
===============
These typically follow the low level fcntl syscalls in Unix (and Win32),
rather than stdio and carry the same names. Note that the z88dk low level
file operations also implement these system calls. Also, some calls,
such as CREAT don't have their own packet in tnfs since they can be
implemented by something else (for example, CREAT is equivalent
to OPEN with the O_CREAT flag). Not all servers will support all flags
for OPEN, but at least O_RDONLY. The mode refers to UNIX file permissions,
see the CHMOD command below.


OPEN
---------------------------------------------------------------------------
> _Opens a file_   
> Command `0x29`

Standard header, flags, mode, then the null terminated filename. Flags
are a bit field.

The flags are:

Flag      | Value  | Description
--------- | ------ | -------------------------------------------------
O_RDONLY  | 0x0001 | Open read only
O_WRONLY  | 0x0002 | Open write only
O_RDWR    | 0x0003 | Open read/write
O_APPEND  | 0x0008 | Append to the file, if it exists (write only)
O_CREAT   | 0x0100 | Create the file if it doesn't exist (write only)
O_TRUNC   | 0x0200 | Truncate the file on open for writing
O_EXCL    | 0x0400 | With O_CREAT, returns an error if the file exists

The modes are the same as described by CHMOD (i.e. POSIX modes). These
may be modified by the server process's umask. The mode only applies
when files are created (if the O_CREAT flag is specified).

Examples:

Open a file called `/foo/bar/baz.bas` for reading:

    0xBEEF 0x00 0x29 0x0001 0x0000 /foo/bar/baz.bas 0x00

Open a file called `/tmp/foo.dat` for writing, creating the file but
returning an error if it exists. Modes set are `S_IRUSR`, `S_IWUSR`,
`S_IRGRP` and `S_IWOTH` (read/write for owner, read-only for group,
read-only for others):

    0xBEEF 0x00 0x29 0x0102 0x01A4 /tmp/foo.dat 0x00

The server returns the standard header and a result code in response.
If the operation was successful, the byte following the result code
is the file descriptor.

Successful file open, file descriptor is 4:

    0xBEEF 0x00 0x29 0x00 0x04

File open failed with "permssion denied":

    0xBEEF 0x00 0x29 0x01

*HISTORICAL NOTE:* OPEN used to have command id 0x20, but with the
addition of extra flags, the id was changed so that servers could
support both the old style OPEN and the new OPEN.


READ
---------------------------------------------------------------------------
> _Reads from a file_   
> Command `0x21`

Reads a block of data from a file. Consists of the standard header
followed by the file descriptor as returned by OPEN, then a 16 bit
little endian integer specifying the size of data that is requested.

The server will only reply with as much data as fits in the maximum
TNFS datagram size when using UDP as a transport. For the
TCP transport, sequencing and buffering etc. are just left up to
the TCP stack, so a READ operation can return blocks of up to 64K. 

If there is less than the size requested remaining in the file, 
the server will return the remainder of the file. Subsequent READ 
commands will return the code EOF.

Examples:

Read from file descriptor 4, maximum of 256 bytes:

    0xBEEF 0x00 0x21 0x04 0x00 0x01

The server will reply with the standard header, followed by the single
byte return code, the actual amount of bytes read as a 16 bit unsigned
little endian value, then the data, for example, 256 bytes:

    0xBEEF 0x00 0x21 0x00 0x00 0x01 ...data...

End-of-file reached:

     0xBEEF 0x00 0x21 0x21


WRITE
---------------------------------------------------------------------------
> _Writes to a file_   
> Command `0x22`

Writes a block of data to a file. Consists of the standard header,
followed by the file descriptor, followed by a 16 bit little endian
value containing the size of the data, followed by the data. The
entire message must fit in a single datagram.

Examples:

Write 256 bytes of data to file descriptor 4:

    0xBEEF 0x00 0x22 0x04 0x00 0x01 ...data...

The server replies with the standard header, followed by the return
code, and the number of bytes actually written. For example:

 Successful write of 256 bytes

    0xBEEF 0x00 0x22 0x00 0x00 0x01
 
 Failed write, error is "bad file descriptor":
 
    0xBEEF 0x00 0x22 0x06


CLOSE
---------------------------------------------------------------------------
> _Closes a file_   
> Command `0x23`

Closes an open file. Consists of the standard header, followed by
the file descriptor.

Example:

 Close file descriptor 4:

    0xBEEF 0x00 0x23 0x04

The server replies with the standard header followed by the return
code.

File closed:

    0xBEEF 0x00 0x23 0x00

Operation failed with EBADF, "bad file descriptor"L

    0xBEEF 0x00 0x23 0x06


STAT
---------------------------------------------------------------------------
> _Get information on a file_   
> Command `0x24`

Reads the file's information, such as size, datestamp etc. The TNFS
stat contains less data than the POSIX stat - information that is unlikely
to be of use to 8 bit systems are omitted.

The request consists of the standard header, followed by the full path
of the file to stat terminated by a NULL.

Example:

    0xBEEF 0x00 0x24 /foo/bar/baz.txt 0x00

The server replies with the standard header, followed by the return code.
On success, the file information follows this. Stat information is returned
in the following order. Not all values are used by all servers. At least file
mode and size must be set to a valid value (many programs depend on these).


    mode      - 2 bytes: File permissions, little endian byte order
    uid       - 2 bytes: Numeric UID of owner
    gid       - 2 bytes: Numeric GID of owner
    size      - 4 bytes: Unsigned 32 bit little endian size of file in bytes
    atime     - 4 bytes: Access time in seconds since the epoch, little endian
    mtime     - 4 bytes: Modification time (as above)
    ctime     - 4 bytes: Time of last status change (as above)
    uidstring - 0 or more bytes: Null terminated user id string
    gidstring - 0 or more bytes: Null terminated group id string

Fields that don't apply to the server in question should be left as 0x00.
The `mtime` field and `size` fields are unsigned 32 bit integers.
The `uidstring` and `gidstring` are helper fields so the client doesn't have
to then ask the server for the string representing the uid and gid.

File mode flags will be most useful for code that is showing a directory
listing, and for programs that need to find out what kind of file (regular
file or directory, etc) a particular file may be. They follow the POSIX
convention which is:

Flags    | Octal   | Description
-------- | ------- | -------------------------------------
S_IFMT   | 0170000 | bitmask for the file type bitfields
S_IFSOCK | 0140000 | is a socket
S_IFLNK  | 0120000 | is a symlink
S_IFREG  | 0100000 | is a regular file
S_IFBLK  | 0060000 | block device
S_IFDIR  | 0040000 | directory
S_IFCHR  | 0020000 | character device
S_IFIFO  | 0010000 | FIFO
S_ISUID  | 0004000 | set UID bit
S_ISGID  | 0002000 | set group ID bit
S_ISVTX  | 0001000 | sticky bit
S_IRWXU  | 00700   | mask for file owner permissions
S_IRUSR  | 00400   | owner has read permission
S_IWUSR  | 00200   | owner has write permission
S_IXUSR  | 00100   | owner has execute permission
S_IRGRP  | 00040   | group has read permission
S_IWGRP  | 00020   | group has write permission
S_IXGRP  | 00010   | group has execute permission
S_IROTH  | 00004   | others have read permission
S_IWOTH  | 00002   | others have write permission
S_IXOTH  | 00001   | others have execute permission

Most of these won't be of much interest to an 8 bit client, but the
read/write/execute permissions can be used for a client to determine whether
to bother even trying to open a remote file, or to automatically execute
certain types of files etc. (Further file metadata such as load and execution
addresses are platform specific and should go into a header of the file
in question). Note the "trivial" bit in TNFS means that the client is
unlikely to do anything special with a FIFO, so writing to a file of that
type is likely to have effects on the server and not the client! It's also
worth noting that the server is responsible for enforcing read and write
permissions (although the permission bits can help the client work out
whether it should bother to send a request).


LSEEK
---------------------------------------------------------------------------
> _Seeks to a new position in a file_  
> Command `0x25`

Seeks to an absolute position in a file, or a relative offset in a file,
or to the end of a file.

The request consists of the header, followed by the file descriptor,
followed by the seek type (`SEEK_SET`, `SEEK_CUR` or `SEEK_END`), followed
by the position to seek to. The seek position is a signed 32 bit integer,
little endian. (2GB file sizes should be more than enough for 8 bit
systems!)

On servers supporting TNFS protocol versions greater than 0x0100,
the reponse includes the current file position after the header
and status byte. This is an unsigned 32-bit little endian value.

The seek types are defined as follows:

    SEEK_SET - Go to an absolute position in the file
    SEEK_CUR - Go to a relative offset from the current position
    SEEK_END - Seek to EOF

Example:

File descriptor is 4, type is SEEK_SET, and position is 0xDEADBEEF:

    0xBEEF 0x00 0x25 0x04 0x00 0xEF 0xBE 0xAD 0xDE

Note that clients that buffer reads for single-byte reads will have
to make a calculation to implement SEEK_CUR correctly since the server's
file pointer will be wherever the last read block made it end up.


UNLINK
---------------------------------------------------------------------------
> _Deletes a file_  
> Command `0x26`

Removes the specified file. The request consists of the header then
the null terminated full path to the file. The reply consists of the
header and the return code.

Example:

Unlink file `/foo/bar/baz.bas`:

    0xBEEF 0x00 0x26 /foo/bar/baz.bas 0x00


CHMOD
---------------------------------------------------------------------------
> _Changes permissions on a file_  
> Command `0x27`

Changes file permissions on the specified file, using POSIX permissions
semantics. Not all permissions may be supported by all servers - most
8 bit systems, for example, may only support removing the write bit.
A server running on something Unix-ish will support everything.

The request consists of the header, followed by the 16 bit file mode,
followed by the null terminated filename. Filemode is sent as a little
endian value. See the Unix manpage for chmod(2) for further information.

File modes are as defined by POSIX. The POSIX definitions are as follows:
              
Flag    | Octal | Description
------- | ----- | ----------------------------
S_ISUID | 04000 | set user id on execution
S_ISGID | 02000 | set group id on execution
S_ISVTX | 01000 | sticky bit
S_IRUSR | 00400 | read by owner
S_IWUSR | 00200 | write by owner
S_IXUSR | 00100 | execute/search by owner
S_IRGRP | 00040 | read by group
S_IWGRP | 00020 | write by group
S_IXGRP | 00010 | execute/search by group
S_IROTH | 00004 | read by others
S_IWOTH | 00002 | write by others
S_IXOTH | 00001 | execute/search by others

Example:

Set permissions to 755 on `/foo/bar/baz.bas`:

    0xBEEF 0x00 0x27 0xED 0x01 /foo/bar/baz.bas

The reply is the standard header plus the return code of the chmod operation.


RENAME
---------------------------------------------------------------------------
> _Moves a file within a filesystem_   
> Command `0x28`

Renames a file (or moves a file within a filesystem - it must be possible
to move a file to a different directory within the same FS on the
server using this command).

The request consists of the header, followed by the null terminated
source path, and the null terminated destination path.

Example:

Move file `foo.txt` to `bar.txt`:

    0xBEEF 0x00 0x28 foo.txt 0x00 bar.txt 0x00


Device Operations
=================
These operations get information about the device that is mounted.


SIZE
---------------------------------------------------------------------------
> _Requests the size of the mounted filesystem_  
> Command `0x30`

Finds the size, in kilobytes, of the filesystem that is currently mounted.
The request consists of a standard header and nothing more.

Example:

    0xBEEF 0x00 0x30

The reply is the standard header, followed by the return code, followed
by a 32 bit little endian integer which is the size of the filesystem
in kilobytes. For example:

Filesystem is 720kbytes:

    0xBEEF 0x00 0x30 0x00 0xD0 0x02 0x00 0x00

Request failed with error code 0xFF:

    0xBEEF 0x00 0x30 0xFF


FREE
---------------------------------------------------------------------------
> _Requests the amount of free space on the filesystem_   
> Command `0x31`

Finds the size, in kilobytes, of the free space remaining on the mounted
filesystem. The request consists of the standard header and nothing more.

Example:

    0xBEEF 0x00 0x31

The reply is as for SIZE - the standard header, return code, and little
endian integer for the free space in kilobytes. For example:

There is 64K free:

    0xBEEF 0x00 0x31 0x00 0x64 0x00 0x00 0x00

Request failed with error 0x1F:

    0xBEEF 0x00 0x31 0x1F


List of Valid Return Codes
==========================
Note not all servers may return all codes. For example, a server on a machine
that doesn't have named pipes will never return ESPIPE.

ID   | POSIX Equiv  | Description
--   | ------------ | -----------------------------------
0x00 |              | Success
0x01 | EPERM        | Operation not permitted
0x02 | ENOENT       | No such file or directory
0x03 | EIO          | I/O error
0x04 | ENXIO        | No such device or address
0x05 | E2BIG        | Argument list too long
0x06 | EBADF        | Bad file number
0x07 | EAGAIN       | Try again
0x08 | ENOMEM       | Out of memory
0x09 | EACCES       | Permission denied
0x0A | EBUSY        | Device or resource busy
0x0B | EEXIST       | File exists
0x0C | ENOTDIR      | Is not a directory
0x0D | EISDIR       | Is a directory
0x0E | EINVAL       | Invalid argument
0x0F | ENFILE       | File table overflow
0x10 | EMFILE       | Too many open files
0x11 | EFBIG        | File too large
0x12 | ENOSPC       | No space left on device
0x13 | ESPIPE       | Attempt to seek on a FIFO or pipe
0x14 | EROFS        | Read only filesystem
0x15 | ENAMETOOLONG | Filename too long
0x16 | ENOSYS       | Function not implemented
0x17 | ENOTEMPTY    | Directory not empty
0x18 | ELOOP        | Too many symbolic links encountered
0x19 | ENODATA      | No data available
0x1A | ENOSTR       | Out of streams resources
0x1B | EPROTO       | Protocol error
0x1C | EBADFD       | File descriptor in bad state
0x1D | EUSERS       | Too many users
0x1E | ENOBUFS      | No buffer space available
0x1F | EALREADY     | Operation already in progress
0x20 | ESTALE       | Stale TNFS handle
0x21 | EOF          | End of file
0xFF |              | Invalid TNFS handle
