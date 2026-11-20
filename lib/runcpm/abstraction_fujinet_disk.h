/**
 * RunCPM disk abstraction for #FujiNet: CP/M drives are folders under /CPM/ on fnSDFAT.
 *
 * Included by each FujiNet abstraction. Everything is RUNCPM_DECL, so a RUNCPM_STATIC_IMPL
 * translation unit keeps its own copy.
 */

#ifndef ABSTRACTION_FUJINET_DISK_H
#define ABSTRACTION_FUJINET_DISK_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "globals.h"
#include "../../include/debug.h"
#include "compat_string.h"
#include "fnFsSD.h"
#ifdef ESP_PLATFORM
#include "../../include/PSRAMAllocator.h"
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

RUNCPM_DECL char _cpm_full_filename[128];

RUNCPM_DECL char *full_path(const char *name)
{
    snprintf(_cpm_full_filename, sizeof(_cpm_full_filename), "/CPM/%s", name);
    return _cpm_full_filename;
}

/* Memory abstraction functions */
/*===============================================================================*/
RUNCPM_DECL success_is_true _RamLoad(char *name, uint16_t address)
{
    FILE *file = fnSDFAT.file_open(full_path(name), "rb");
    success_is_true result(false);
    uint8_t value;

    if (file)
    {
        while (!feof(file))
        {
            if (fread(&value, sizeof(uint8_t), 1, file) == 1)
            {
                _RamWrite(address++, value);
                result = success_is_true(true);
            }
            else
                result = success_is_true(false);
        }
        fclose(file);
    }
    Debug_printf("CCP last address: %04x\r\n", address);
    return result;
}

/* filesystem (disk) abstraction functions */
/*===============================================================================*/

// CP/M moves 128 bytes per call, so the host files stay open between calls rather than
// being reopened for each record. A BDOS close, delete, rename or make closes one; the CCP
// closes the rest before each command.
#define CPM_OPEN_FILES 4

struct cpm_open_file
{
    char name[sizeof(filename)];
    FILE *file;
    long size;
    bool writable;
    bool dirty;
    uint32_t lastUse;
};

// In PSRAM, and only once CP/M opens its first file
#ifdef ESP_PLATFORM
static std::vector<cpm_open_file, PSRAMAllocator<cpm_open_file>> _cpm_open_files;
#else
static std::vector<cpm_open_file> _cpm_open_files;
#endif
static uint32_t _cpm_file_uses;
static const uint8_t _cpm_zero_record[BlkSZ] = {0};

static void _cpm_file_close(cpm_open_file &entry)
{
    if (entry.file)
        fclose(entry.file);
    entry = cpm_open_file();
}

RUNCPM_DECL void _sys_closeall()
{
    for (cpm_open_file &entry : _cpm_open_files)
        _cpm_file_close(entry);
}

static cpm_open_file *_cpm_file_find(const char *name)
{
    for (cpm_open_file &entry : _cpm_open_files)
    {
        if (entry.file && strcmp(entry.name, name) == 0)
        {
            entry.lastUse = ++_cpm_file_uses;
            return &entry;
        }
    }
    return nullptr;
}

// Close before a remove, rename or truncate: with FS_LOCK off, FatFs would later write the
// old handle's size into the freed directory slot.
RUNCPM_DECL void _sys_closefile(uint8_t *name)
{
    cpm_open_file *entry = _cpm_file_find((char *)name);
    if (entry)
        _cpm_file_close(*entry);
}

// A directory search reads sizes from FatFs directory entries, which a file updates only when
// it is closed
static void _cpm_file_close_dirty()
{
    for (cpm_open_file &entry : _cpm_open_files)
    {
        if (entry.dirty)
            _cpm_file_close(entry);
    }
}

static FILE *_cpm_file_open(const char *name, const char *mode)
{
    FILE *file = fnSDFAT.file_open(full_path(name), mode);
    if (file || !fnSDFAT.exists(full_path(name)))
        return file;

    // It is there, so the open-file table is probably full: give ours back and retry
    _sys_closeall();
    return fnSDFAT.file_open(full_path(name), mode);
}

static cpm_open_file *_cpm_file_slot()
{
    if (_cpm_open_files.empty())
        _cpm_open_files.resize(CPM_OPEN_FILES);

    cpm_open_file *oldest = &_cpm_open_files[0];
    for (cpm_open_file &entry : _cpm_open_files)
    {
        if (!entry.file)
            return &entry;
        if (entry.lastUse < oldest->lastUse)
            oldest = &entry;
    }
    _cpm_file_close(*oldest);
    return oldest;
}

// Unbuffered, so the size kept here is always the file's and a failed write fails its record
static cpm_open_file *_cpm_file_keep(const char *name, FILE *file)
{
    struct stat info;
    if (setvbuf(file, nullptr, _IONBF, 0) != 0 || fstat(fileno(file), &info) != 0)
    {
        fclose(file);
        return nullptr;
    }

    cpm_open_file *entry = _cpm_file_slot();
    strlcpy(entry->name, name, sizeof(entry->name));
    entry->file = file;
    entry->size = info.st_size;
    entry->lastUse = ++_cpm_file_uses;
    return entry;
}

static cpm_open_file *_cpm_file_for_read(const char *name)
{
    cpm_open_file *entry = _cpm_file_find(name);
    if (entry)
        return entry;

    FILE *file = _cpm_file_open(name, "rb");
    return file ? _cpm_file_keep(name, file) : nullptr;
}

static cpm_open_file *_cpm_file_for_write(const char *name)
{
    cpm_open_file *entry = _cpm_file_find(name);
    if (entry && entry->writable)
        return entry;
    if (entry)
        _cpm_file_close(*entry);

    // A write creates a file that isn't there
    const char *mode = fnSDFAT.exists(full_path(name)) ? "r+b" : "w+b";
    FILE *file = _cpm_file_open(name, mode);
    if (!file)
        return nullptr;

    entry = _cpm_file_keep(name, file);
    if (entry)
        entry->writable = true;
    return entry;
}

static uint8_t _cpm_file_read(const char *name, long fpos)
{
    cpm_open_file *entry = _cpm_file_for_read(name);
    if (!entry)
        return 0x10;

    // EOF. Never seek past it: FatFs grows a writable file to meet the seek.
    if (fpos >= entry->size)
        return 0x01;

    if (fseek(entry->file, fpos, SEEK_SET) != 0)
    {
        _cpm_file_close(*entry);
        return 0x01;
    }

    // set DMA buffer to EOF
    uint8_t dmabuf[BlkSZ];
    memset(dmabuf, 0x1a, BlkSZ);
    size_t bytes = fread(dmabuf, 1, BlkSZ, entry->file);
    if (bytes > 0)
        memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
    else if (ferror(entry->file))
        _cpm_file_close(*entry);
    return bytes > 0 ? 0x00 : 0x01;
}

static uint8_t _cpm_file_write(const char *name, long fpos, uint8_t seekError)
{
    cpm_open_file *entry = _cpm_file_for_write(name);
    if (!entry)
        return 0xff;

    // Zero any gap: a seek that grows a file on FatFs leaves the old bytes from the card
    if (entry->size < fpos && fseek(entry->file, entry->size, SEEK_SET) != 0)
    {
        _cpm_file_close(*entry);
        return 0xff;
    }
    while (entry->size < fpos)
    {
        size_t gap = std::min<long>(fpos - entry->size, BlkSZ);
        if (fwrite(_cpm_zero_record, 1, gap, entry->file) != gap)
        {
            _cpm_file_close(*entry);
            return 0xff;
        }
        entry->size += gap;
        entry->dirty = true;
    }

    if (fseek(entry->file, fpos, SEEK_SET) != 0)
    {
        _cpm_file_close(*entry);
        return seekError;
    }
    if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, 1, entry->file) != 1)
    {
        _cpm_file_close(*entry);
        return 0xff;
    }
    entry->size = std::max<long>(entry->size, fpos + BlkSZ);
    entry->dirty = true;
    return 0x00;
}

RUNCPM_DECL bool _sys_exists(uint8 *name)
{
    return fnSDFAT.exists(full_path((char *)name));
}

RUNCPM_DECL int _sys_fputc(uint8_t chr, FILE *file)
{
    return fputc(chr, file);
}

RUNCPM_DECL void _sys_fflush(FILE *file)
{
    fflush(file);
}

RUNCPM_DECL void _sys_fclose(FILE *file)
{
    fclose(file);
}

RUNCPM_DECL int _sys_select(uint8_t *disk)
{
    return fnSDFAT.exists(full_path((char *)disk));
}

RUNCPM_DECL long _sys_filesize(uint8_t *name)
{
    cpm_open_file *entry = _cpm_file_find((char *)name);
    if (entry)
        return entry->size;

    ssize_t size = fnSDFAT.filesize(full_path((char *)name));
    return size;
}

RUNCPM_DECL int _sys_openfile(uint8_t *name)
{
    return _cpm_file_for_read((char *)name) ? 1 : 0;
}

RUNCPM_DECL int _sys_makefile(uint8_t *name)
{
    _sys_closefile(name);
    FILE *file = fnSDFAT.file_open(full_path((char *)name), "wb");
    if (file)
    {
        fclose(file);
        return 1;
    }
    return 0;
}

RUNCPM_DECL success_is_true _sys_deletefile(uint8_t *name)
{
    _sys_closefile(name);
    return fnSDFAT.remove(full_path((char *)name));
}

RUNCPM_DECL success_is_true _sys_renamefile(uint8_t *name, uint8_t *newname)
{
    _sys_closefile(name);
    _sys_closefile(newname);
    std::string from(full_path((char *)name));
    std::string dest(full_path((char *)newname));

    return fnSDFAT.rename(from.c_str(), dest.c_str());
}

RUNCPM_DECL void _sys_logbuffer(uint8_t *buffer)
{
    // not implemented at present.
    (void)buffer;
}

RUNCPM_DECL uint8_t _sys_readseq(uint8_t *name, long fpos)
{
    return _cpm_file_read((char *)name, fpos);
}

RUNCPM_DECL uint8_t _sys_writeseq(uint8_t *name, long fpos)
{
    return _cpm_file_write((char *)name, fpos, 0x01);
}

RUNCPM_DECL uint8_t _sys_readrand(uint8_t *name, long fpos)
{
    return _cpm_file_read((char *)name, fpos);
}

RUNCPM_DECL uint8_t _sys_writerand(uint8_t *name, long fpos)
{
    return _cpm_file_write((char *)name, fpos, 0x06);
}

RUNCPM_DECL uint8_t findNextDirName[17];
RUNCPM_DECL uint16_t fileRecords = 0;
RUNCPM_DECL uint16_t fileExtents = 0;
RUNCPM_DECL uint16_t fileExtentsUsed = 0;
RUNCPM_DECL uint16_t firstFreeAllocBlock;

// A host name CP/M can address: 1-8 characters, optionally a dot and 0-3 more
RUNCPM_DECL bool _is_cpm_name(const char *name)
{
    const char *dot = strchr(name, '.');
    size_t base_len = dot ? static_cast<size_t>(dot - name) : strlen(name);

    if (base_len < 1 || base_len > 8)
        return false;
    return !dot || (strchr(dot + 1, '.') == nullptr && strlen(dot + 1) <= 3);
}

RUNCPM_DECL uint8_t _findnext(uint8_t isdir)
{
    uint8_t result = 0xff;
    fsdir_entry *entry;

    if (allExtents && fileRecords)
    {
        _mockupDirEntry();
        return 0;
    }

    while ((entry = fnSDFAT.dir_read()))
    {
        if (entry->isDir || !_is_cpm_name(entry->filename))
            continue;
        size_t name_len = std::string_view(entry->filename).copy(
            reinterpret_cast<char *>(findNextDirName), sizeof(findNextDirName) - 1);
        findNextDirName[name_len] = 0;
        uint32_t bytes = entry->size;
        _HostnameToFCBname(findNextDirName, fcbname);
        if (match(fcbname, pattern))
        {
            if (isdir)
            {
                // account for host files that aren't multiples of the block size
                // by rounding their bytes up to the next multiple of blocks
                if (bytes & (BlkSZ - 1))
                    bytes = (bytes & ~(BlkSZ - 1)) + BlkSZ;
                fileRecords = bytes / BlkSZ;
                fileExtents = fileRecords / BlkEX + ((fileRecords & (BlkEX - 1)) ? 1 : 0);
                fileExtentsUsed = 0;
                firstFreeAllocBlock = firstBlockAfterDir;
                _mockupDirEntry();
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
    return result;
}

RUNCPM_DECL uint8_t _findfirst(uint8_t isdir)
{
    uint8_t path[4] = {'?', FOLDERCHAR, '?', 0};
    path[0] = filename[0];
    path[2] = filename[2];
    _cpm_file_close_dirty();
    fnSDFAT.dir_close();
    fnSDFAT.dir_open(full_path((char *)path), "*", 0);
    _HostnameToFCBname(filename, pattern);
    fileRecords = 0;
    fileExtents = 0;
    fileExtentsUsed = 0;
    return _findnext(isdir);
}

RUNCPM_DECL uint8_t _findnextallusers(uint8_t isdir)
{
    return _findnext(isdir);
}

RUNCPM_DECL uint8_t _findfirstallusers(uint8_t isdir)
{
    strcpy((char *)pattern, "???????????");
    fileRecords = 0;
    fileExtents = 0;
    fileExtentsUsed = 0;
    return _findnextallusers(isdir);
}

RUNCPM_DECL uint8_t _Truncate(char *name, uint8_t records)
{
    // Implement some other way.
    (void)name;
    (void)records;
    return 0;
}

RUNCPM_DECL void _MakeUserDir()
{
    uint8_t dFolder = cDrive + 'A';
    uint8_t uFolder = toupper(tohex(userCode));
    uint8_t path[4] = {dFolder, FOLDERCHAR, uFolder, 0};

    if (fnSDFAT.exists(full_path((char *)path)))
        return;

    fnSDFAT.create_path(full_path((char *)path));
}

RUNCPM_DECL uint8_t _sys_makedisk(uint8_t drive)
{
    if (drive < 1 || drive > 16)
        return 0xff;

    uint8_t dFolder = drive + '@';
    uint8_t disk[2] = {dFolder, 0};

    if (fnSDFAT.exists(full_path((char *)disk)))
        return 0;

    if (fnSDFAT.create_path(full_path((char *)disk)).is_error())
        return 0xfe;

    uint8_t path[4] = {dFolder, FOLDERCHAR, '0', 0};
    fnSDFAT.create_path(full_path((char *)path));
    return 0;
}

#endif /* ABSTRACTION_FUJINET_DISK_H */
