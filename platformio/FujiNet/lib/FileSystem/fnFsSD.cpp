/* TODO: Check why using the SD/FAT routines takes up a large amount of the stack (around 4.5K)
*/

#include <memory>
#include <time.h>
#include <esp_vfs.h>
#include "esp_vfs_fat.h"

#include "fnFsSD.h"
#include "../../include/debug.h"

#define SD_HOST_CS GPIO_NUM_5
#define SD_HOST_MISO GPIO_NUM_19
#define SD_HOST_MOSI GPIO_NUM_23
#define SD_HOST_SCK GPIO_NUM_18

// Our global SD interface
FileSystemSDFAT fnSDFAT;

bool FileSystemSDFAT::dir_open(const char * path)
{
    FRESULT result = f_opendir(&_dir, path);
    return (result == FR_OK);
}
void FileSystemSDFAT::dir_close()
{
    f_closedir(&_dir);
}

fsdir_entry * FileSystemSDFAT::dir_read()
{
    FILINFO finfo;
    FRESULT result = f_readdir(&_dir, &finfo);

    if(result != FR_OK || finfo.fname[0] == '\0')
        return nullptr;

    strncpy(_direntry.filename, finfo.fname, sizeof(_direntry.filename));

    _direntry.isDir = finfo.fattrib & AM_DIR ? true : false;
    _direntry.size = finfo.fsize;

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

    tmtime.tm_sec = (finfo.ftime & TIMEBITS_SECOND) * 2;
    tmtime.tm_min = (finfo.ftime & TIMEBITS_MINUTE) >> 5;
    tmtime.tm_hour = (finfo.ftime & TIMEBITS_HOUR) >> 11;

    tmtime.tm_mday = (finfo.fdate & DATEBITS_DAY);
    tmtime.tm_mon = ((finfo.fdate & DATEBITS_MONTH) >> 5) -1; // tm_mon = months 0-11
    tmtime.tm_year = ((finfo.fdate & DATEBITS_YEAR) >> 9) + 80; //tm_year = years since 1900

    tmtime.tm_isdst = 0;

    #ifdef DEBUG
    /*
        Debug_printf("FileSystemSDFAT direntry: \"%s\"\n", _direntry.filename);
        Debug_printf("FileSystemSDFAT date (0x%04x): yr=%d, mn=%d, da=%d; time (0x%04x) hr=%d, mi=%d, se=%d\n", 
            finfo.fdate,
            tmtime.tm_year, tmtime.tm_mon, tmtime.tm_mday,
            finfo.ftime,
            tmtime.tm_hour, tmtime.tm_min, tmtime.tm_sec);
    */
    #endif

    _direntry.modified_time = mktime(&tmtime);
    
    return &_direntry;
}

FILE * FileSystemSDFAT::file_open(const char* path, const char* mode)
{
    //Debug_printf("sdfileopen1: task hwm %u, %p\n", uxTaskGetStackHighWaterMark(NULL), pxTaskGetStackStart(NULL));
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    //Debug_printf("sdfileopen2: task hwm %u, %p\n", uxTaskGetStackHighWaterMark(NULL), pxTaskGetStackStart(NULL));
#ifdef DEBUG
    //Debug_printf("fopen = %s\n", result == nullptr ? "err" : "ok");
#endif    
    return result;
}

bool FileSystemSDFAT::exists(const char* path)
{
    FRESULT result = f_stat(path, NULL);
#ifdef DEBUG
    //Debug_printf("sdFileSystem::exists returned %d on \"%s\"\n", result, path);
#endif
    return (result == FR_OK);
}

bool FileSystemSDFAT::remove(const char* path)
{
    FRESULT result = f_unlink(path);
#ifdef DEBUG
    //Debug_printf("sdFileSystem::remove returned %d on \"%s\"\n", result, path);
#endif
    return (result == FR_OK);
}

bool FileSystemSDFAT::rename(const char* pathFrom, const char* pathTo)
{
    FRESULT result = f_rename(pathFrom, pathTo);
#ifdef DEBUG
    Debug_printf("sdFileSystem::rename returned %d on \"%s\" -> \"%s\"\n", result, pathFrom, pathTo);
#endif
    return (result == FR_OK);
}

uint64_t FileSystemSDFAT::card_size()
{
    return _card_capacity;
}

uint64_t FileSystemSDFAT::total_bytes()
{
	FATFS* fsinfo;
	DWORD fre_clust;
    uint64_t size = 0ULL;

	if (f_getfree("0:", &fre_clust, &fsinfo) == 0)
    {
        // cluster_size * num_clusters * sector_size
        size = ((uint64_t)(fsinfo->csize)) * (fsinfo->n_fatent - 2) * (fsinfo->ssize);
    }
	return size;
}

uint64_t FileSystemSDFAT::used_bytes()
{
	FATFS* fsinfo;
	DWORD fre_clust;
    uint64_t size = 0ULL;

	if(f_getfree("0:", &fre_clust, &fsinfo) == 0)
    {
        // cluster_size * (num_clusters - free_clusters) * sector_size
	    size = ((uint64_t)(fsinfo->csize)) * ((fsinfo->n_fatent-2)-(fsinfo->free_clst)) * (fsinfo->ssize);
    }
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

	FATFS* fsinfo;
	DWORD fre_clust;

    BYTE i = 0;
 	if(f_getfree("0:", &fre_clust, &fsinfo) == 0)
    {
        if(fsinfo->fs_type >= FS_FAT12 && fsinfo->fs_type <= FS_EXFAT)
            i = fsinfo->fs_type;
    }
    return names[i];
}

bool FileSystemSDFAT::start()
{
    if(_started)
        return true;

    // Set our basepath
    strncpy(_basepath, "/sd", sizeof(_basepath));

    // Set up a configuration to the SD host interface
    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();
    host_config.max_freq_khz = 4000000; // from Arduino SD.h

    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_HOST_CS;
    slot_config.gpio_miso = SD_HOST_MISO;
    slot_config.gpio_mosi = SD_HOST_MOSI;
    slot_config.gpio_sck = SD_HOST_SCK;

    // Fat FS configuration options
    esp_vfs_fat_mount_config_t mount_config;
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;

    // This is the information we'll be given in return
    sdmmc_card_t *sdcard_info;

    esp_err_t e = esp_vfs_fat_sdmmc_mount(
        _basepath,
        &host_config,
        &slot_config,
        &mount_config,
        &sdcard_info
    );

    if(e == ESP_OK)
    {
        _started = true;
        _card_capacity = (uint64_t)sdcard_info->csd.capacity * sdcard_info->csd.sector_size;
    #ifdef DEBUG
        Debug_println("SD mounted.");
    /*
        Debug_printf("  manufacturer: %d, oem: 0x%x \"%c%c\"\n", sdcard_info->cid.mfg_id, sdcard_info->cid.oem_id,
            (char)(sdcard_info->cid.oem_id >> 8 & 0xFF),(char)(sdcard_info->cid.oem_id & 0xFF));
        Debug_printf("  product: %s\n", sdcard_info->cid.name);
        Debug_printf("  sector size: %d, sectors: %d, capacity: %llu\n", sdcard_info->csd.sector_size, sdcard_info->csd.capacity, _card_capacity);
        Debug_printf("  transfer speed: %d\n", sdcard_info->csd.tr_speed);
        Debug_printf("  max frequency: %ukHz\n", sdcard_info->max_freq_khz);
        Debug_printf("  partition type: %s\n", partition_type());
        Debug_printf("  partition size: %llu, used: %llu\n", total_bytes(), used_bytes());
    */
    #endif
    }
    else 
    {
        _started = false;
        _card_capacity = 0;
    #ifdef DEBUG
        Debug_printf("SD mount failed with code #%d, \"%s\"\n", e, esp_err_to_name(e));
    #endif
    }

    return _started;
}
