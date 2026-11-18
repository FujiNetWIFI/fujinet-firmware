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
#include <string>

#include "globals.h"
#include "../../include/debug.h"
#include "fnFsSD.h"

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

RUNCPM_DECL char *full_path(char *name)
{
    snprintf(_cpm_full_filename, sizeof(_cpm_full_filename), "/CPM/%s", name);
    return _cpm_full_filename;
}

/* Memory abstraction functions */
/*===============================================================================*/
RUNCPM_DECL success_is_true _RamLoad(char *name, uint16_t address)
{
    FILE *file = fnSDFAT.file_open(full_path(name), "r");
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
    long size = -1;
    FILE *file = fnSDFAT.file_open(full_path((char *)name), "r");

    if (file)
    {
        fseek(file, 0L, SEEK_END);
        size = ftell(file);
        fclose(file);
    }
    return size;
}

RUNCPM_DECL int _sys_openfile(uint8_t *name)
{
    FILE *file = fnSDFAT.file_open(full_path((char *)name), "r");
    if (file)
    {
        fclose(file);
        return 1;
    }
    return 0;
}

RUNCPM_DECL int _sys_makefile(uint8_t *name)
{
    FILE *file = fnSDFAT.file_open(full_path((char *)name), "w");
    if (file)
    {
        fclose(file);
        return 1;
    }
    return 0;
}

RUNCPM_DECL int _sys_deletefile(uint8_t *name)
{
    return fnSDFAT.remove(full_path((char *)name));
}

RUNCPM_DECL int _sys_renamefile(uint8_t *name, uint8_t *newname)
{
    std::string from(full_path((char *)name));
    std::string dest(full_path((char *)newname));

    return fnSDFAT.rename(from.c_str(), dest.c_str());
}

RUNCPM_DECL void _sys_logbuffer(uint8_t *buffer)
{
    // not implemented at present.
    (void)buffer;
}

RUNCPM_DECL success_is_true _sys_extendfile(char *name, unsigned long fpos)
{
    FILE *file = fnSDFAT.file_open(full_path(name), "a");

    if (!file)
        RETURN_ERROR_AS_FALSE();

    ssize_t origSize = fnSDFAT.filesize(full_path(name));

    if (fpos > (unsigned long)origSize)
    {
        for (long idx = 0; idx < (long)(fpos - origSize); ++idx)
        {
            if (fwrite("\0", sizeof(uint8_t), 1, file) != 1)
            {
                fclose(file);
                RETURN_ERROR_AS_FALSE();
            }
        }
    }
    fclose(file);
    RETURN_SUCCESS_AS_TRUE();
}

RUNCPM_DECL uint8_t _sys_readseq(uint8_t *name, long fpos)
{
    uint8_t result = 0xff;
    FILE *file = fnSDFAT.file_open(full_path((char *)name), "r");

    if (!file)
        return 0x10;

    int seekErr = fseek(file, fpos, SEEK_SET);
    if (fpos > 0 && seekErr != 0)
    {
        // EOF
        result = 0x01;
    }
    else
    {
        // set DMA buffer to EOF
        uint8_t dmabuf[BlkSZ];
        memset(dmabuf, 0x1a, BlkSZ);
        uint8_t bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), file);
        if (bytesread)
            memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
        result = bytesread ? 0x00 : 0x01;
    }
    fclose(file);
    return result;
}

RUNCPM_DECL uint8_t _sys_writeseq(uint8_t *name, long fpos)
{
    uint8_t result = 0xff;
    FILE *file;

    if (_sys_extendfile((char *)name, fpos).is_success())
        file = fnSDFAT.file_open(full_path((char *)name), "r+");
    else
        return result;

    if (file)
    {
        if (fseek(file, fpos, SEEK_SET) == 0)
        {
            if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, sizeof(uint8_t), file))
                result = 0x00;
        }
        else
            result = 0x01;
        fclose(file);
    }
    else
        result = 0x10;

    return result;
}

RUNCPM_DECL uint8_t _sys_readrand(uint8_t *name, long fpos)
{
    uint8_t result = 0xff;
    FILE *file = fnSDFAT.file_open(full_path((char *)name), "r+");

    if (file)
    {
        if (fseek(file, fpos, SEEK_SET) == 0)
        {
            uint8_t dmabuf[BlkSZ];
            memset(dmabuf, 0x1A, BlkSZ);
            uint8_t bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), file);
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
                long extSize = _sys_filesize(name);

                // round file size up to next full logical extent
                extSize = ExtSZ * ((extSize / ExtSZ) + ((extSize % ExtSZ) ? 1 : 0));
                if (fpos < extSize)
                    result = 0x01; // reading unwritten data
                else
                    result = 0x04; // seek to unwritten extent
            }
        }
        fclose(file);
    }
    else
        result = 0x10;

    return result;
}

RUNCPM_DECL uint8_t _sys_writerand(uint8_t *name, long fpos)
{
    uint8_t result = 0xff;
    FILE *file;

    if (_sys_extendfile((char *)name, fpos).is_success())
        file = fnSDFAT.file_open(full_path((char *)name), "r+");
    else
        return result;

    if (file)
    {
        if (fseek(file, fpos, SEEK_SET) == 0)
        {
            if (fwrite(_RamSysAddr(dmaAddr), BlkSZ, sizeof(uint8_t), file))
                result = 0x00;
        }
        else
            result = 0x06;
        fclose(file);
    }
    else
        result = 0x10;

    return result;
}

RUNCPM_DECL uint8_t findNextDirName[17];
RUNCPM_DECL uint16_t fileRecords = 0;
RUNCPM_DECL uint16_t fileExtents = 0;
RUNCPM_DECL uint16_t fileExtentsUsed = 0;
RUNCPM_DECL uint16_t firstFreeAllocBlock;

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
        strcpy((char *)findNextDirName, entry->filename); // careful watch for string overflow!
        if (entry->isDir)
            continue;
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

    if (!fnSDFAT.create_path(full_path((char *)disk)))
        return 0xfe;

    uint8_t path[4] = {dFolder, FOLDERCHAR, '0', 0};
    fnSDFAT.create_path(full_path((char *)path));
    return 0;
}

#endif /* ABSTRACTION_FUJINET_DISK_H */
