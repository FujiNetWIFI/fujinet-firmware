/**
 * Abstraction functions for #FujiNet — SHARED CORE variant.
 *
 * This is the disk/SD + BDOS-helper section of abstraction_fujinet.h, made
 * console-transport-agnostic so it can be compiled EXACTLY ONCE inside the
 * single shared RunCPM core (lib/runcpm/runcpm_core.cpp).
 *
 * Differences from abstraction_fujinet.h:
 *   - The SIO-specific console section (the _cpm_txbuf batched writer, the
 *     teeMode/client/server mirror, and the direct SYSTEM_BUS _getch/_getche/
 *     _putch) is REPLACED by thin indirection shims that dispatch through the
 *     active transport's runcpm_console_ops (g_runcpm_console).  Each transport
 *     (SIO 'G', N:CPM://, telnet console) supplies its own ops.
 *   - The TCP/tee BIOS helpers (bios_tcpListen/Available/TeeAccept/Drop) and
 *     their client/server/teeMode/portActive globals live in the SIO transport
 *     (siocpm.cpp), not here, because they are SIO-console specific.
 *   - The Atari-only bdos_* config helpers (networkConfig/readHostSlots/
 *     readDeviceSlots) are dropped: they have no callers anywhere in the tree
 *     and were the only thing tying this core to theFuji/fujiDevice.h/fnWiFi,
 *     which would have prevented the shared core from compiling on the non-Atari
 *     FujiNet targets that also expose the N:CPM:// device.
 *
 * The disk/SD family is byte-for-byte identical to abstraction_fujinet.h; only
 * one copy now exists in the firmware image.
 */

#ifndef ABSTRACTION_FUJINET_CORE_H
#define ABSTRACTION_FUJINET_CORE_H

#include <string.h>
#include <errno.h>
#include "compat_string.h"

#include "globals.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFsSD.h"

#include "runcpm_session.h"

#define HostOS 0x07 // FUJINET

/* FujiNet vendoring: the per-transport CP/M headers (siocpm.h, the network
   abstraction, etc.) used to define FOLDERCHAR for the chain.  Now that the
   chain is compiled once here in the shared core, define it locally.  FujiNet
   stores CP/M disks under "/CPM/<drive>/<user>/" on the SD card, so the path
   separator is the POSIX forward slash. */
#ifndef FOLDERCHAR
#define FOLDERCHAR '/'
#endif

/* FujiNet vendoring: RunCPM 6.9 disk.h::_mockupDirEntry references FILEBASE
   (the host path prefix) to optionally strip it from enumerated names. FujiNet
   stores bare filenames (no prefix) and always calls _mockupDirEntry(0), so
   define FILEBASE empty to satisfy compilation with zero-length prefix. */
#ifndef FILEBASE
#define FILEBASE ""
#endif

/* FujiNet vendoring: RunCPM 6.9 calls millis() unconditionally (CPU throttle in
   cpu.h, Z80estimateClock in cpu_mhz.h, BDOS F_UPTIME C=248 in cpm.h). In 5.8
   millis() was only used under #ifdef PROFILE (never compiled). Map it to the
   FujiNet system uptime clock, mirroring upstream's macro abstraction. */
#ifndef millis
#define millis() ((uint32)fnSystem.millis())
#endif

typedef struct
{
    uint8_t dr;
    uint8_t fn[8];
    uint8_t tp[3];
    uint8_t ex, s1, s2, rc;
    uint8_t al[16];
    uint8_t cr, r0, r1, r2;
} CPM_FCB;

typedef struct
{
    uint8_t dr;
    uint8_t fn[8];
    uint8_t tp[3];
    uint8_t ex, s1, s2, rc;
    uint8_t al[16];
} CPM_DIRENTRY;

int dirPos;

char full_filename[128];

char *full_path(char *fn)
{
    memset(full_filename, 0, sizeof(full_filename));
    strcpy(full_filename, "/CPM/");
    strcat(full_filename, fn);
    return full_filename;
}


//
// Hardware functions, new in 5.x
//
void _HardwareOut(const uint32 Port, const uint32 Value)
{

}

uint32 _HardwareIn(const uint32 Port)
{
    return 0;
}

/* Memory abstraction functions */
/*===============================================================================*/
/* FujiNet vendoring: RunCPM 6.9 changed _RamLoad to
   `uint16 _RamLoad(uint8 *filename, uint16 address, uint16 maxsize)` returning
   the number of bytes read (maxsize == 0 means "no limit"). ccp.h's AUTOEXEC
   path relies on the byte count, so honor maxsize and return the count. */
uint16 _RamLoad(uint8 *filename, uint16 address, uint16 maxsize)
{
    FILE *f = fnSDFAT.file_open(full_path((char *)filename), "r");
    uint16 count = 0;
    uint8_t b;

    if (f)
    {
        while ((!maxsize || count < maxsize) && fread(&b, sizeof(uint8_t), 1, f) == 1)
        {
            _RamWrite(address++, b);
            count++;
        }
        fclose(f);
    }
    return (count);
}

/* filesystem (disk) abstraction fuctions */
/*===============================================================================*/
FILE *rootdir;
FILE *userdir;

bool _sys_exists(uint8* filename)
{
    return fnSDFAT.exists(full_path((char *)filename));
}

int _sys_fputc(uint8_t ch, FILE *f)
{
    return fputc(ch, f);
}

void _sys_fflush(FILE *f)
{
    fflush(f);
}

void _sys_fclose(FILE *f)
{
    fclose(f);
}

int _sys_select(uint8_t *disk)
{
    return fnSDFAT.exists(full_path((char *)disk));
}

long _sys_filesize(uint8_t *fn)
{
    unsigned long fs = -1;
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "r");

    if (fp)
    {
        fseek(fp, 0L, SEEK_END);
        fs = ftell(fp);
    }

    fclose(fp);
    return fs;
}

int _sys_openfile(uint8_t *fn)
{
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "r");
    if (fp)
    {
        fclose(fp);
        return 1;
    }
    else
        return 0;
}

int _sys_makefile(uint8_t *fn)
{
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "w");
    if (fp)
    {
        fclose(fp);
        return true;
    }
    else
        return false;
}

int _sys_deletefile(uint8_t *fn)
{
    return fnSDFAT.remove(full_path((char *)fn));
}

int _sys_renamefile(uint8_t *fn, uint8_t *newname)
{
    std::string from, to;

    from = std::string(full_path((char *)fn));
    to = std::string(full_path((char *)newname));

    return fnSDFAT.rename(from.c_str(), to.c_str());
}

void _sys_logbuffer(uint8_t *buffer)
{
    // not implemented at present.
}

bool _sys_extendfile(char *fn, unsigned long fpos)
{
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "a");

    if (!fp)
        return false;

    long origSize = fnSDFAT.filesize(full_path(fn));

    // This was patterned after the arduino abstraction, and I do not like how this works.

    if (fpos > origSize)
    {
        for (long i = 0; i < (origSize - fpos); ++i)
        {
            if (fwrite("\0", sizeof(uint8_t), 1, fp) != 1)
            {
                fclose(fp);
                return false;
            }
        }
    }
    fclose(fp);
    return true;
}

uint8_t _sys_readseq(uint8_t *fn, long fpos)
{
    uint8_t result = 0xff;
    FILE *f;
    uint8_t bytesread;
    uint8_t dmabuf[BlkSZ];
    int seekErr;

    f = fnSDFAT.file_open(full_path((char *)fn), "r");
    if (!f)
    {
        result = 0x10;
        return result;
    }
    seekErr = fseek(f, fpos, SEEK_SET);
    if (f)
    {
        if (fpos > 0 && seekErr != 0)
        {
            // EOF
            result = 0x01;
        }
        else
        {
            // set DMA buffer to EOF
            memset(dmabuf, 0x1a, BlkSZ);
            // BUG FIX (2026-06-21): this was fread(dmabuf, BlkSZ, 1, f), i.e.
            // size=128, nmemb=1.  fread() returns the number of *complete*
            // elements read, so a final PARTIAL record (file size not a
            // multiple of 128) made it return 0: the bytes that WERE read were
            // discarded (the `if (bytesread)` memcpy was skipped) and result
            // was reported as 0x01 (EOF).  Symptom: TYPE / ASM / LOAD / PIP
            // silently dropped the last partial 128-byte record of every file
            // whose length was not a multiple of 128 -- a 127-byte file showed
            // nothing at all, a 202-byte file was truncated at exactly 128
            // bytes.  Fix: read byte-wise (size=1, nmemb=BlkSZ) so fread
            // returns the byte count (1..128) for partial records too; dmabuf
            // is already pre-filled with 0x1a so the short record is padded
            // with CP/M EOF markers as the BDOS sequential read expects.
            bytesread = fread(&dmabuf[0], sizeof(uint8_t), BlkSZ, f);
            if (bytesread)
                memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
            result = bytesread ? 0x00 : 0x01;
        }
    }
    else
    {
        result = 0x10;
    }
    fclose(f);
    return (result);
}

uint8_t _sys_writeseq(uint8_t *fn, long fpos)
{
    uint8_t result = 0xff;
    FILE *f;

    if (_sys_extendfile((char *)fn, fpos))
        f = fnSDFAT.file_open(full_path((char *)fn), "r+");
    else
        return result;

    if (f)
    {
        if (fseek(f, fpos, SEEK_SET) == 0)
        {
            if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, sizeof(uint8_t), f))
                result = 0x00;
        }
        else
        {
            result = 0x01;
        }
    }
    else
    {
        result = 0x10;
    }
    fclose(f);
    return (result);
}

uint8_t _sys_readrand(uint8_t *fn, long fpos)
{
    uint8 result = 0xff;
    FILE *f;
    uint8 bytesread;
    uint8 dmabuf[BlkSZ];
    long extSize;

    f = fnSDFAT.file_open(full_path((char *)fn), "r+");
    if (f)
    {
        if (fseek(f, fpos, SEEK_SET) == 0)
        {
            memset(dmabuf, 0x1A, BlkSZ);
            // BUG FIX (2026-06-21): same partial-record fread bug as
            // _sys_readseq above -- fread(dmabuf, BlkSZ, 1, f) returns 0 for a
            // final record shorter than 128 bytes, dropping its data and
            // signalling premature EOF.  Read byte-wise so the count (1..128)
            // is returned; dmabuf is pre-filled with 0x1A as EOF padding.
            bytesread = fread(&dmabuf[0], sizeof(uint8_t), BlkSZ, f);
            if (bytesread)
                memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
            result = bytesread ? 0x00 : 0x01;
        }
        else
        {
            if (fpos >= 65536L * BlkSZ)
            {
                result = 0x06; // seek past 8MB (largest file size in CP/M)
            }
            else
            {
                extSize = _sys_filesize((uint8_t *)full_path((char *)fn));

                // round file size up to next full logical extent
                extSize = ExtSZ * ((extSize / ExtSZ) + ((extSize % ExtSZ) ? 1 : 0));
                if (fpos < extSize)
                    result = 0x01; // reading unwritten data
                else
                    result = 0x04; // seek to unwritten extent
            }
        }
    }
    else
    {
        result = 0x10;
    }
    fclose(f);
    return (result);
}

uint8_t _sys_writerand(uint8_t *fn, long fpos)
{
    uint8 result = 0xff;
    FILE *f;

    if (_sys_extendfile((char *)fn, fpos))
    {
        f = fnSDFAT.file_open(full_path((char *)fn), "r+");
    }
    else
        return result;

    if (f)
    {
        if (fseek(f, fpos, SEEK_SET) == 0)
        {
            if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, sizeof(uint8_t), f))
                result = 0x00;
        }
        else
        {
            result = 0x06;
        }
    }
    else
    {
        result = 0x10;
    }
    fclose(f);
    return (result);
}

uint8_t findNextDirName[17];
uint16_t fileRecords = 0;
uint16_t fileExtents = 0;
uint16_t fileExtentsUsed = 0;
uint16_t firstFreeAllocBlock;

uint8_t _findnext(uint8_t isdir)
{
    uint8 result = 0xff;
    bool isfile;
    uint32 bytes;
    fsdir_entry *entry;

    if (allExtents && fileRecords)
    {
        _mockupDirEntry(0); // FujiNet vendoring: mode 0 = bare filename (no FILEBASE prefix)
        result = 0;
    }
    else
    {
        while ((entry = fnSDFAT.dir_read()))
        {
            strcpy((char *)findNextDirName, entry->filename); // careful watch for string overflow!
            isfile = !entry->isDir;
            bytes = entry->size;
            if (!isfile)
                continue;
            _HostnameToFCBname(findNextDirName, fcbname);
            if (match(fcbname, pattern))
            {
                if (isdir)
                {
                    // account for host files that aren't multiples of the block size
                    // by rounding their bytes up to the next multiple of blocks
                    if (bytes & (BlkSZ - 1))
                    {
                        bytes = (bytes & ~(BlkSZ - 1)) + BlkSZ;
                    }
                    fileRecords = bytes / BlkSZ;
                    fileExtents = fileRecords / BlkEX + ((fileRecords & (BlkEX - 1)) ? 1 : 0);
                    fileExtentsUsed = 0;
                    firstFreeAllocBlock = firstBlockAfterDir;
                    _mockupDirEntry(0); // FujiNet vendoring: mode 0 = bare filename (no FILEBASE prefix)
                }
                else
                {
                    fileRecords = 0;
                    fileExtents = 0;
                    fileExtentsUsed = 0;
                    firstFreeAllocBlock = firstBlockAfterDir;
                }
                _RamWrite(tmpFCB, filename[0] - '@');
                _HostnameToFCB(tmpFCB, findNextDirName);
                result = 0x00;
                break;
            }
        }
    }
    return (result);
}

uint8_t _findfirst(uint8_t isdir)
{
    uint8 path[4] = {'?', FOLDERCHAR, '?', 0};
    path[0] = filename[0];
    path[2] = filename[2];
    fnSDFAT.dir_close();
    fnSDFAT.dir_open(full_path((char *)path), "*", 0);
    _HostnameToFCBname(filename, pattern);
    fileRecords = 0;
    fileExtents = 0;
    fileExtentsUsed = 0;
    return (_findnext(isdir));
}

uint8_t _findnextallusers(uint8_t isdir)
{
    return _findnext(isdir);
}

uint8_t _findfirstallusers(uint8_t isdir)
{
    dirPos = 0;
    strcpy((char *)pattern, "???????????");
    fileRecords = 0;
    fileExtents = 0;
    fileExtentsUsed = 0;
    return (_findnextallusers(isdir));
}

uint8_t _Truncate(char *fn, uint8_t rc)
{
    // Implement some other way.
    return 0;
}

void _MakeUserDir()
{
    uint8 dFolder = cDrive + 'A';
    uint8 uFolder = toupper(tohex(userCode));

    uint8 path[4] = {dFolder, FOLDERCHAR, uFolder, 0};

    if (fnSDFAT.exists(full_path((char *)path)))
    {
        return;
    }

    fnSDFAT.create_path(full_path((char *)path));
}

uint8_t _sys_makedisk(uint8_t drive)
{
    uint8 result = 0;
    if (drive < 1 || drive > 16)
    {
        result = 0xff;
    }
    else
    {
        uint8 dFolder = drive + '@';
        uint8 disk[2] = {dFolder, 0};

        if (fnSDFAT.exists(full_path((char *)disk)))
            return 0;

        if (!fnSDFAT.create_path(full_path((char *)disk)))
        {
            result = 0xfe;
        }
        else
        {
            uint8 path[4] = {dFolder, FOLDERCHAR, '0', 0};
            fnSDFAT.create_path(full_path((char *)path));
        }
    }
    return (result);
}

/* Console abstraction functions */
/*===============================================================================*/
/*
 * Transport-agnostic console: every console primitive the RunCPM chain
 * (console.h, cpm.h, ccp.h) calls is dispatched through the active transport's
 * runcpm_console_ops, installed by runcpm_session_run() before the CCP starts.
 * g_runcpm_console is defined in runcpm_core.cpp.
 */
extern "C" runcpm_console_ops g_runcpm_console;

#define _kbhit() (g_runcpm_console.kbhit())

static inline uint8 _getch(void)
{
    return (uint8)g_runcpm_console.getch();
}

static inline uint8 _getche(void)
{
    return (uint8)g_runcpm_console.getche();
}

static inline void _putch(uint8 ch)
{
    g_runcpm_console.putch(ch);
}

static inline void _clrscr(void)
{
    g_runcpm_console.clrscr();
}

#endif /* ABSTRACTION_FUJINET_CORE_H */
