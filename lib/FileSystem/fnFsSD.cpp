/* TODO: Check why using the SD/FAT routines takes up a large amount of the stack (around 4.5K)
*/

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "fnFsSD.h"
#include "fnFileLocal.h"

#ifdef ESP_PLATFORM
#include <esp_vfs.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <esp_rom_gpio.h>
#include <soc/sdmmc_periph.h>
#endif

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "compat_string.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "utils.h"

#ifdef ESP_PLATFORM
  #include <esp_idf_version.h>
  #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
  #define SDSPI_DEFAULT_DMA 1
  #endif
#else
// !ESP_PLATFORM
  #if defined(_WIN32)
    #include <direct.h>
    static inline int mkdir(const char *path, mode_t mode)
    {
        return _mkdir(path);
    }
  #endif // _WIN32
// !ESP_PLATFORM
#endif

// Our global SD interface
FileSystemSDFAT fnSDFAT;


#ifdef ESP_PLATFORM
/*
  Converts the FatFs ftime and fdate to a POSIX time_t value
*/
time_t _fssd_fatdatetime_to_epoch(WORD ftime, WORD fdate)
{
    // Convert date and time

    // 5 bits 4-0 = seconds / 2 (0-29)
    #define TIMEBITS_SECOND 0x001FUL
    // 6 bits 10-5 = minutes (0-59)
    #define TIMEBITS_MINUTE 0x07E0UL
    // 5 bits 15-11 = hour (0-23)
    #define TIMEBITS_HOUR 0xF800UL

    // 5 bits 4-0 = day (0-31)
    #define DATEBITS_DAY 0x001FUL
    // 4 bits 8-5 = month (1-12)
    #define DATEBITS_MONTH 0x01E0UL
    // 7 bits 15-9 = year from 1980 (0-127)
    #define DATEBITS_YEAR 0xFE00UL

    struct tm tmtime;

    tmtime.tm_sec = (ftime & TIMEBITS_SECOND) * 2;
    tmtime.tm_min = (ftime & TIMEBITS_MINUTE) >> 5;
    tmtime.tm_hour = (ftime & TIMEBITS_HOUR) >> 11;

    tmtime.tm_mday = (fdate & DATEBITS_DAY);
    tmtime.tm_mon = ((fdate & DATEBITS_MONTH) >> 5) -1; // tm_mon = months 0-11
    tmtime.tm_year = ((fdate & DATEBITS_YEAR) >> 9) + 80; //tm_year = years since 1900

    tmtime.tm_isdst = 0;

    return mktime(&tmtime);
}
#endif

bool FileSystemSDFAT::is_dir(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat info;
    // st_mode carries the permission bits too, so compare the file type field
    // rather than the whole word - a plain "== S_IFDIR" is never true on hosts
    // whose stat() fills those in.
    bool result = (stat(fpath, &info) == 0) && S_ISDIR(info.st_mode);
    free(fpath);
    return result;
}

success_is_true FileSystemSDFAT::mkdir(const char* path)
{
    char * fpath = _make_fullpath(path);
    Debug_printf("FileSystemSDFAT::mkdir \"%s\" (\"%s\")\r\n", path, fpath);

    int result = ::mkdir(fpath, S_IRWXU);
    free(fpath);
    if(0 != result)
    {
        Debug_printf("  mkdir failed: errno %d\r\n", errno);
    }
    RETURN_SUCCESS_IF(0 == result);
}

success_is_true FileSystemSDFAT::rmdir(const char* path)
{
    char * fpath = _make_fullpath(path);
    Debug_printf("FileSystemSDFAT::rmdir \"%s\" (\"%s\")\r\n", path, fpath);

    int result = ::rmdir(fpath);
    free(fpath);
    if(0 != result)
    {
        Debug_printf("  rmdir failed: errno %d\r\n", errno);
    }
    RETURN_SUCCESS_IF(0 == result);
}

success_is_true FileSystemSDFAT::fill_dircache(const char *path)
{
#ifndef ESP_PLATFORM
    Debug_printf("FileSystemSDFAT::fill_dircache \"%s\"\n", path);
#endif

#ifdef ESP_PLATFORM
    FRESULT result = f_opendir(&_dir, path);
    if(result != FR_OK)
        RETURN_ERROR_AS_FALSE();
#else
    char * fpath = _make_fullpath(path);
    Debug_printf("FileSystemSDFAT::fill_dircache - opendir \"%s\"\n", fpath);
    _dir = opendir(fpath);
    free(fpath);
    if(_dir == nullptr)
        RETURN_ERROR_AS_FALSE();
#endif

#ifdef ESP_PLATFORM
    FILINFO finfo;

    while(f_readdir(&_dir, &finfo) == FR_OK)
    {
        // An empty name indicates the end of the directory
        if(finfo.fname[0] == '\0')
            break;
        // Ignore items starting with '.'
        if(finfo.fname[0] == '.')
            continue;
        // Ignore items marked hidden or system
        if(finfo.fattrib & AM_HID || finfo.fattrib & AM_SYS)
            continue;

        // Ignore some special files we create on SD
        if(strcmp(finfo.fname, "paper") == 0 
        || strcmp(finfo.fname, "fnconfig.ini") == 0
        || strcmp(finfo.fname, "rs232dump") == 0)
            continue;

        if (_dircache.add_entry(finfo.fname, finfo.fattrib & AM_DIR, finfo.fsize,
                                _fssd_fatdatetime_to_epoch(finfo.ftime, finfo.fdate)).is_error())
        {
            f_closedir(&_dir);
            RETURN_ERROR_AS_FALSE();
        }
    }
// ESP_PLATFORM
#else
// !ESP_PLATFORM
    struct dirent *d;
    struct stat s;

    while((d = readdir(_dir)) != nullptr)
    {
        // Ignore items starting with '.'
        if(d->d_name[0] == '.')
            continue;
        // Ignore some special files we create on SD
        if(strcmp(d->d_name, "paper") == 0 
        || strcmp(d->d_name, "fnconfig.ini") == 0
        || strcmp(d->d_name, "rs232dump") == 0)
            continue;

        uint32_t size = 0;
        time_t modified_time = 0;
        fpath = _make_fullpath(d->d_name);
        if(stat(fpath, &s) == 0)
        {
            size = s.st_size;
            modified_time = s.st_mtime;
        }
        free(fpath);
        // Assume symlinks point to directories only
        if (_dircache.add_entry(d->d_name, d->d_type == DT_DIR || d->d_type == DT_LNK, size, modified_time).is_error())
        {
            closedir(_dir);
            RETURN_ERROR_AS_FALSE();
        }
    }
// !ESP_PLATFORM
#endif

    // Future operations will be performed on the cache
#ifdef ESP_PLATFORM
    f_closedir(&_dir);
#else
    closedir(_dir);
#endif

    RETURN_SUCCESS_AS_TRUE();
}


FILE * FileSystemSDFAT::file_open(const char* path, const char* mode)
{
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    Debug_printf("fopen = %s %s : %s\r\n", path, mode, result == nullptr ? "err" : "ok");
    return result;
}

#ifndef FNIO_IS_STDIO
FileHandler * FileSystemSDFAT::filehandler_open(const char* path, const char* mode)
{
    FILE * fh = file_open(path, mode);
    return (fh == nullptr) ? nullptr : new FileHandlerLocal(fh);
}
#endif

bool FileSystemSDFAT::exists(const char* path)
{
#ifdef ESP_PLATFORM
    FRESULT result = f_stat(path, NULL);
    return (result == FR_OK);
#else
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
    free(fpath);
    return (i == 0);
#endif
}

file_size_t FileSystemSDFAT::filesize(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
    file_size_t res((0 == i) ? st.st_size : -1);
#ifdef DEBUG
    Debug_printf("FileSystemSDFAT::filesize returned %lld on \"%s\" (\"%s\")\r\n", (long long)res, path, fpath);
#endif
    free(fpath);
    return res;
}

success_is_true FileSystemSDFAT::remove(const char* path)
{
#ifdef ESP_PLATFORM
    FRESULT result = f_unlink(path);
    RETURN_SUCCESS_IF(result == FR_OK);
#else
    char * fpath = _make_fullpath(path);
    int result = ::remove(fpath);
    free(fpath);
    RETURN_SUCCESS_IF(0 == result);
#endif
}

long FileSystemSDFAT::mtime(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
    long res = (0 == i) ? st.st_mtime : -1;
    free(fpath);
    return res;
}

/* Checks that path exists and creates it if it doesn't, including any parent directories.
   cumulativePath holds the path prefix built up to the current segment (for mkdir),
   so it can be as long as the full path.
   An initial "/" is optional, but you should not include an ending "/".

   Examples:
   "abc"
   "/abc"
   "/abc/def"
   "abc/def/ghi"
*/
success_is_true FileSystemSDFAT::create_path(const char *path)
{
    /* Holds the path prefix built up to the current segment (for mkdir). */
    char cumulativePath[MAX_PATHLEN];

#ifdef ESP_PLATFORM
    const char *fullpath = path;
#else
    char *fullpath = _make_fullpath(path);
#endif
    const char *end = fullpath;
    bool done = false;

    while (!done)
    {
        bool found = false;

        if(*end == '\0')
        {
            done = true;
            if(end != fullpath)
                found = true;
        } else if(*end == '/')
        {
            if(end != fullpath)
                found = true;
        }

        if(found)
        {
            size_t len = (size_t)(end - fullpath + (done ? 2 : 1));
            if (len > sizeof(cumulativePath))
            {
                Debug_printf("create_path: path prefix too long\r\n");
#ifndef ESP_PLATFORM
                free(fullpath);
#endif
                RETURN_ERROR_AS_FALSE();
            }
            strlcpy(cumulativePath, fullpath, len);
#ifdef ESP_PLATFORM
            if ( !exists(cumulativePath) )
            {
                if(0 != f_mkdir(cumulativePath))
                {
                    Debug_printf("FAILED errno=%d\r\n", errno);
                    RETURN_ERROR_AS_FALSE();
                }
            }
#else
            if(0 != ::mkdir(cumulativePath, S_IRWXU))
            {
                if(errno != EEXIST)
                {
                    Debug_printf("FAILED errno=%d\r\n", errno);
                    free(fullpath);
                    RETURN_ERROR_AS_FALSE();
                }
            }
#endif
        }

        end++;
    }
#ifndef ESP_PLATFORM
    free(fullpath);
#endif

    RETURN_SUCCESS_AS_TRUE();
}

success_is_true FileSystemSDFAT::rename(const char* pathFrom, const char* pathTo)
{
#ifdef ESP_PLATFORM
    FRESULT result = f_rename(pathFrom, pathTo);
    Debug_printf("FileSystemSDFAT::rename returned %d on \"%s\" -> \"%s\"\r\n", result, pathFrom, pathTo);
    RETURN_SUCCESS_IF(result == FR_OK);
#else
    char * spath = _make_fullpath(pathFrom);
    char * dpath = _make_fullpath(pathTo);
    int i = ::rename(spath, dpath);
    Debug_printf("FileSystemSDFAT::rename returned %d on \"%s\" -> \"%s\" (%s -> %s)\r\n", i, pathFrom, pathTo, spath, dpath);
    free(spath);
    free(dpath);
    RETURN_SUCCESS_IF(i == 0);
#endif
}

uint64_t FileSystemSDFAT::card_size()
{
    return _card_capacity;
}

uint64_t FileSystemSDFAT::total_bytes()
{
    uint64_t size = 0ULL;
#ifdef ESP_PLATFORM
	FATFS* fsinfo;
	DWORD fre_clust;

	if (f_getfree("0:", &fre_clust, &fsinfo) == 0)
    {
        // cluster_size * num_clusters * sector_size
        size = ((uint64_t)(fsinfo->csize)) * (fsinfo->n_fatent - 2) * (fsinfo->ssize);
    }
#endif
	return size;
}

uint64_t FileSystemSDFAT::used_bytes()
{
    uint64_t size = 0ULL;
#ifdef ESP_PLATFORM
	FATFS* fsinfo;
	DWORD fre_clust;

	if(f_getfree("0:", &fre_clust, &fsinfo) == 0)
    {
        // cluster_size * (num_clusters - free_clusters) * sector_size
	    size = ((uint64_t)(fsinfo->csize)) * ((fsinfo->n_fatent-2)-(fsinfo->free_clst)) * (fsinfo->ssize);
    }
#endif
	return size;
}

const char * FileSystemSDFAT::partition_type()
{
    static const char *names[] = 
    {
        "UNKNOWN",
        "FAT12",
        "FAT16",
        "FAT32",
        "EXFAT"
    };
    unsigned char i = 0;

#ifdef ESP_PLATFORM
	FATFS* fsinfo;
	DWORD fre_clust;

 	if(f_getfree("0:", &fre_clust, &fsinfo) == 0)
    {
        if(fsinfo->fs_type >= FS_FAT12 && fsinfo->fs_type <= FS_EXFAT)
            i = fsinfo->fs_type;
    }
#endif
    return names[i];
}

#ifdef ESP_PLATFORM
/*
  Some SD cards answer CMD59 (CRC_ON_OFF) with the R1 illegal-command bit set, which the
  sdspi layer reports as ESP_ERR_NOT_SUPPORTED (0x106). ESP-IDF runs that step
  (sdmmc_init_spi_crc) unconditionally for every card brought up over SPI and treats a
  failure as fatal, so sdmmc_card_init() aborts and the card never mounts:

    E (xxxx) sdmmc_sd: sdmmc_init_spi_crc: sdmmc_send_cmd_crc_on_off returned 0x106
    E (xxxx) vfs_fat_sdmmc: sdmmc_card_init failed (0x106).

  CRC16 of data transfers is optional in SPI mode per the SD Physical Layer spec (it is only
  mandatory in SD/native mode), which is why these cards work under the Arduino SD library -
  its SPI diskio never sends CMD59 at all. IDF offers no Kconfig option or sdmmc_host_t flag
  to skip the step, so it is redirected here with "-Wl,--wrap=sdmmc_init_spi_crc" (see the
  build_flags in platformio-ini-files/platformio.common.ini) and made non-fatal: cards that
  accept CMD59 keep CRC on, cards that reject it mount with CRC off instead of failing.

  Only the SPI path reaches this step - in SD/4-bit mode CRC is mandatory and handled by the
  hardware - so the SDMMC branch of start() below is unaffected.

  See https://github.com/FujiNetWIFI/fujinet-firmware/issues/1625
*/
extern "C"
{
    // Exported by the sdmmc component, declared in its private header
    // esp_private/sdmmc_common.h - declared here so we don't have to reach into that header.
    esp_err_t sdmmc_send_cmd_crc_on_off(sdmmc_card_t *card, bool crc_enable);

    esp_err_t __wrap_sdmmc_init_spi_crc(sdmmc_card_t *card)
    {
        // CMD59 is attempted directly rather than through __real_sdmmc_init_spi_crc(), which
        // remains linked, because that one does an ESP_LOGE on failure - printing the alarming
        // "returned 0x106" line on every boot with an affected card.
        esp_err_t e = sdmmc_send_cmd_crc_on_off(card, true);
        if(e != ESP_OK)
            Debug_printf("SD card rejected CMD59/CRC_ON_OFF (%s), continuing with SPI CRC off\r\n", esp_err_to_name(e));

        return ESP_OK; // never fatal
    }
}

success_is_true FileSystemSDFAT::start()
{
    if(_started)
        RETURN_SUCCESS_AS_TRUE();

    // Set our basepath
    strlcpy(_basepath, "/sd", sizeof(_basepath));

    // Fat FS configuration options
    esp_vfs_fat_mount_config_t mount_config;
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 16;

    // This is the information we'll be given in return
    sdmmc_card_t *sdcard_info;

#ifdef SDMMC_HOST_WIDTH

    sdmmc_host_t host_config = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SDMMC_HOST_WIDTH;
    slot_config.clk = PIN_SD_HOST_CLK;
    slot_config.cmd = PIN_SD_HOST_CMD;
    slot_config.d0  = PIN_SD_HOST_D0;
    slot_config.d1  = PIN_SD_HOST_D1;
    slot_config.d2  = PIN_SD_HOST_D2;
    slot_config.d3  = PIN_SD_HOST_D3;
    slot_config.wp  = PIN_SD_HOST_WP;

    esp_err_t e = esp_vfs_fat_sdmmc_mount(_basepath, &host_config, &slot_config, &mount_config, &sdcard_info);

#if SDMMC_HOST_WP_LEVEL
    // Override WP routing of GPIO to SDMMC peripheral in order to omit inversion - the original routing is located at
    // https://github.com/espressif/esp-idf/blob/51772f4fb5c2bbe25b60b4a51d707fa2afd3ac75/components/driver/sdmmc/sdmmc_host.c#L508-L510
    esp_rom_gpio_connect_in_signal(PIN_SD_HOST_WP, sdmmc_slot_info[host_config.slot].write_protect, false);
#endif

#else /* SDMMC_HOST_WIDTH */

    // Set up a configuration to the SD host interface
    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT(); 

    // Set up SPI bus
    spi_bus_config_t bus_cfg = 
    {
        .mosi_io_num = PIN_SD_HOST_MOSI,
        .miso_io_num = PIN_SD_HOST_MISO,
        .sclk_io_num = PIN_SD_HOST_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
#ifdef BUILD_APPLE
        .max_transfer_sz = 27000
#else
        .max_transfer_sz = 4000
#endif
    };

    spi_bus_initialize(SDSPI_DEFAULT_HOST ,&bus_cfg, SDSPI_DEFAULT_DMA);

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_SD_HOST_CS;
    slot_config.host_id = SDSPI_DEFAULT_HOST;

    esp_err_t e = esp_vfs_fat_sdspi_mount(_basepath, &host_config, &slot_config, &mount_config, &sdcard_info);

#endif /* SDMMC_HOST_WIDTH */

    if(e == ESP_OK)
    {
        _started = true;
        _card_capacity = (uint64_t)sdcard_info->csd.capacity * sdcard_info->csd.sector_size;
        Debug_println("SD mounted.");
    }
    else 
    {
        _started = false;
        _card_capacity = 0;
        Debug_printf("SD mount failed with code #%d, \"%s\"\r\n", e, esp_err_to_name(e));
    }

    RETURN_SUCCESS_IF(_started);
}
#else
// !ESP_PLATFORM
success_is_true FileSystemSDFAT::start(const char *sd_path)
{
    if(_started)
        RETURN_SUCCESS_AS_TRUE();

    // Set our basepath
    if (sd_path)
        strlcpy(_basepath, sd_path, sizeof(_basepath));
    else
        strlcpy(_basepath, "SD", sizeof(_basepath));

    _card_capacity = 0;

    // test if _basepath directory exists
    struct stat st;
    int result = stat(_basepath, &st);
    if (0 == result)
    {
        _started = true;
        Debug_printf("SD mounted (directory \"%s\").\r\n", _basepath);
    }
    else
    {
        Debug_printf("SD mount failed, directory \"%s\" does not exist\r\n", _basepath);
    }

    RETURN_SUCCESS_IF(_started);
}
// !ESP_PLATFORM
#endif
