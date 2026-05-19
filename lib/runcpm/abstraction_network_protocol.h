/**
 * RunCPM I/O abstraction for NetworkProtocol CPM adapter.
 *
 * Console I/O is routed through per-TU static queues (_cpm_rxq / _cpm_txq).
 * Filesystem I/O uses fnSDFAT, identical to the other FujiNet abstractions.
 *
 * Include this AFTER #define RUNCPM_STATIC_IMPL, BEFORE other RunCPM headers.
 * All symbols are static so this TU coexists with bus-device CPM TUs.
 */

#ifndef ABSTRACTION_NETWORK_PROTOCOL_H
#define ABSTRACTION_NETWORK_PROTOCOL_H

#include <string.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "../../include/debug.h"
#include "fnFsSD.h"

#define FOLDERCHAR '/'
#define HostOS 0x07  // FUJINET

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

/* -------------------------------------------------------------------------
 * Queue storage — TU-local due to RUNCPM_STATIC_IMPL / static keyword.
 *
 * _cpm_txq : user → CPM stdin   (write() pushes; _getch() pops)
 * _cpm_rxq : CPM stdout → user  (_putch() pushes; read() pops)
 * ------------------------------------------------------------------------- */
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
static QueueHandle_t _cpm_rxq = nullptr;
static QueueHandle_t _cpm_txq = nullptr;
#else
#include <queue>
#include <mutex>
#include <condition_variable>
static std::queue<uint8_t> _cpm_rxq;
static std::queue<uint8_t> _cpm_txq;
static std::mutex _cpm_rxmtx;
static std::mutex _cpm_txmtx;
static std::condition_variable _cpm_txcv;
#endif

/* Set by _cpm_run() when the CCP exits of its own accord (e.g. user typed EXIT).
 * Polled by NetworkProtocolCPM::status() to drive EOF back to the host. */
static volatile bool _cpm_session_ended = false;

/* -------------------------------------------------------------------------
 * Path helpers
 * ------------------------------------------------------------------------- */
static char _cpm_full_filename[128];

static char *full_path(char *fn)
{
    memset(_cpm_full_filename, 0, sizeof(_cpm_full_filename));
    strcpy(_cpm_full_filename, "/CPM/");
    strcat(_cpm_full_filename, fn);
    return _cpm_full_filename;
}

/* -------------------------------------------------------------------------
 * Hardware stubs (required by RunCPM cpu.h)
 * ------------------------------------------------------------------------- */
static void _HardwareOut(const uint32 Port, const uint32 Value) { (void)Port; (void)Value; }
static uint32 _HardwareIn(const uint32 Port) { (void)Port; return 0; }

/* -------------------------------------------------------------------------
 * Memory abstraction
 * ------------------------------------------------------------------------- */
static bool _RamLoad(char *fn, uint16_t address)
{
    FILE *f = fnSDFAT.file_open(full_path(fn), "r");
    bool result = false;
    uint8_t b;

    if (f)
    {
        while (!feof(f))
        {
            if (fread(&b, sizeof(uint8_t), 1, f) == 1)
            {
                _RamWrite(address++, b);
                result = true;
            }
            else
                result = false;
        }
        fclose(f);
    }
    Debug_printf("CCP last address: %04x\r\n", address);
    return result;
}

/* -------------------------------------------------------------------------
 * Filesystem abstraction  (mirrors abstraction_fujinet_apple2.h)
 * ------------------------------------------------------------------------- */
static FILE *rootdir;
static FILE *userdir;

static bool _sys_exists(uint8 *filename)
{
    return fnSDFAT.exists(full_path((char *)filename));
}

static int _sys_fputc(uint8_t ch, FILE *f) { return fputc(ch, f); }
static void _sys_fflush(FILE *f) { fflush(f); }
static void _sys_fclose(FILE *f) { fclose(f); }

static int _sys_select(uint8_t *disk)
{
    return fnSDFAT.exists(full_path((char *)disk));
}

static long _sys_filesize(uint8_t *fn)
{
    long fs = -1;
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "r");
    if (fp)
    {
        fseek(fp, 0L, SEEK_END);
        fs = ftell(fp);
        fclose(fp);
    }
    return fs;
}

static int _sys_openfile(uint8_t *fn)
{
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "r");
    if (fp) { fclose(fp); return 1; }
    return 0;
}

static int _sys_makefile(uint8_t *fn)
{
    FILE *fp = fnSDFAT.file_open(full_path((char *)fn), "w");
    if (fp) { fclose(fp); return 1; }
    return 0;
}

static int _sys_deletefile(uint8_t *fn)
{
    return fnSDFAT.remove(full_path((char *)fn));
}

static int _sys_renamefile(uint8_t *fn, uint8_t *newname)
{
    std::string from(full_path((char *)fn));
    std::string to(full_path((char *)newname));
    return fnSDFAT.rename(from.c_str(), to.c_str());
}

static void _sys_logbuffer(uint8_t *buffer) { (void)buffer; }

static bool _sys_extendfile(char *fn, unsigned long fpos)
{
    FILE *fp = fnSDFAT.file_open(full_path(fn), "a");
    if (!fp) return false;

    long origSize = fnSDFAT.filesize(full_path(fn));
    if (fpos > (unsigned long)origSize)
    {
        for (long i = 0; i < (long)(fpos - origSize); ++i)
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

static uint8_t _sys_readseq(uint8_t *fn, long fpos)
{
    uint8_t result = 0xff;
    FILE *f = fnSDFAT.file_open(full_path((char *)fn), "r");
    if (!f) return 0x10;

    int seekErr = fseek(f, fpos, SEEK_SET);
    if (fpos > 0 && seekErr != 0)
    {
        result = 0x01;
    }
    else
    {
        uint8_t dmabuf[BlkSZ];
        memset(dmabuf, 0x1a, BlkSZ);
        uint8_t bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), f);
        if (bytesread)
            memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
        result = bytesread ? 0x00 : 0x01;
    }
    fclose(f);
    return result;
}

static uint8_t _sys_writeseq(uint8_t *fn, long fpos)
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
            result = 0x01;
        fclose(f);
    }
    else
        result = 0x10;

    return result;
}

static uint8_t _sys_readrand(uint8_t *fn, long fpos)
{
    uint8_t result = 0xff;
    FILE *f = fnSDFAT.file_open(full_path((char *)fn), "r+");
    if (f)
    {
        if (fseek(f, fpos, SEEK_SET) == 0)
        {
            uint8_t dmabuf[BlkSZ];
            memset(dmabuf, 0x1A, BlkSZ);
            uint8_t bytesread = fread(&dmabuf[0], BlkSZ, sizeof(uint8_t), f);
            if (bytesread)
                memcpy((uint8_t *)&RAM[dmaAddr], dmabuf, BlkSZ);
            result = bytesread ? 0x00 : 0x01;
        }
        else
        {
            if (fpos >= 65536L * BlkSZ)
            {
                result = 0x06;
            }
            else
            {
                long extSize = _sys_filesize((uint8_t *)full_path((char *)fn));
                extSize = ExtSZ * ((extSize / ExtSZ) + ((extSize % ExtSZ) ? 1 : 0));
                result = (fpos < extSize) ? 0x01 : 0x04;
            }
        }
        fclose(f);
    }
    else
        result = 0x10;

    return result;
}

static uint8_t _sys_writerand(uint8_t *fn, long fpos)
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
            result = 0x06;
        fclose(f);
    }
    else
        result = 0x10;

    return result;
}

static uint8_t findNextDirName[17];
static uint16_t fileRecords     = 0;
static uint16_t fileExtents     = 0;
static uint16_t fileExtentsUsed = 0;
static uint16_t firstFreeAllocBlock;

static uint8_t _findnext(uint8_t isdir)
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
        strcpy((char *)findNextDirName, entry->filename);
        if (entry->isDir) continue;
        uint32_t bytes = entry->size;
        _HostnameToFCBname(findNextDirName, fcbname);
        if (match(fcbname, pattern))
        {
            if (isdir)
            {
                if (bytes & (BlkSZ - 1))
                    bytes = (bytes & ~(BlkSZ - 1)) + BlkSZ;
                fileRecords       = bytes / BlkSZ;
                fileExtents       = fileRecords / BlkEX + ((fileRecords & (BlkEX - 1)) ? 1 : 0);
                fileExtentsUsed   = 0;
                firstFreeAllocBlock = firstBlockAfterDir;
                _mockupDirEntry();
            }
            else
            {
                fileRecords = fileExtents = fileExtentsUsed = 0;
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

static uint8_t _findfirst(uint8_t isdir)
{
    uint8_t path[4] = {'?', FOLDERCHAR, '?', 0};
    path[0] = filename[0];
    path[2] = filename[2];
    fnSDFAT.dir_close();
    fnSDFAT.dir_open(full_path((char *)path), "*", 0);
    _HostnameToFCBname(filename, pattern);
    fileRecords = fileExtents = fileExtentsUsed = 0;
    return _findnext(isdir);
}

static uint8_t _findnextallusers(uint8_t isdir) { return _findnext(isdir); }

static uint8_t _findfirstallusers(uint8_t isdir)
{
    strcpy((char *)pattern, "???????????");
    fileRecords = fileExtents = fileExtentsUsed = 0;
    return _findnextallusers(isdir);
}

static uint8_t _Truncate(char *fn, uint8_t rc) { (void)fn; (void)rc; return 0; }

static void _MakeUserDir()
{
    uint8_t dFolder = cDrive + 'A';
    uint8_t uFolder = toupper(tohex(userCode));
    uint8_t path[4] = {dFolder, FOLDERCHAR, uFolder, 0};

    if (!fnSDFAT.exists(full_path((char *)path)))
        fnSDFAT.create_path(full_path((char *)path));
}

static uint8_t _sys_makedisk(uint8_t drive)
{
    if (drive < 1 || drive > 16) return 0xff;

    uint8_t dFolder = drive + '@';
    uint8_t disk[2] = {dFolder, 0};

    if (fnSDFAT.exists(full_path((char *)disk))) return 0;
    if (!fnSDFAT.create_path(full_path((char *)disk))) return 0xfe;

    uint8_t path[4] = {dFolder, FOLDERCHAR, '0', 0};
    fnSDFAT.create_path(full_path((char *)path));
    return 0;
}

/* -------------------------------------------------------------------------
 * Console abstraction — bridges RunCPM I/O to the per-TU queues
 * ------------------------------------------------------------------------- */

static int _kbhit(void)
{
#ifdef ESP_PLATFORM
    if (_cpm_txq == nullptr) return 0;
    return (int)uxQueueMessagesWaiting(_cpm_txq);
#else
    std::lock_guard<std::mutex> lk(_cpm_txmtx);
    return (int)_cpm_txq.size();
#endif
}

static uint8_t _getch(void)
{
    uint8_t c = 0;
#ifdef ESP_PLATFORM
    if (_cpm_txq != nullptr)
        xQueueReceive(_cpm_txq, &c, portMAX_DELAY);
#else
    std::unique_lock<std::mutex> lk(_cpm_txmtx);
    _cpm_txcv.wait(lk, [] { return !_cpm_txq.empty(); });
    c = _cpm_txq.front();
    _cpm_txq.pop();
#endif
    return c;
}

static uint8_t _getche(void)
{
    uint8_t c = _getch();
    /* echo back through rxq so the terminal sees it */
#ifdef ESP_PLATFORM
    if (_cpm_rxq != nullptr)
        xQueueSend(_cpm_rxq, &c, portMAX_DELAY);
#else
    {
        std::lock_guard<std::mutex> lk(_cpm_rxmtx);
        _cpm_rxq.push(c);
    }
#endif
    return c;
}

static void _putch(uint8_t ch)
{
#ifdef ESP_PLATFORM
    if (_cpm_rxq != nullptr)
        xQueueSend(_cpm_rxq, &ch, portMAX_DELAY);
#else
    {
        std::lock_guard<std::mutex> lk(_cpm_rxmtx);
        _cpm_rxq.push(ch);
    }
#endif
}

static void _clrscr(void)
{
    /* VT100 cursor-home + clear-screen */
    _putch(0x1B); _putch('['); _putch('1'); _putch(';');
    _putch('1');  _putch('H'); _putch(0x1B); _putch('[');
    _putch('2');  _putch('J');
}

#endif /* ABSTRACTION_NETWORK_PROTOCOL_H */
