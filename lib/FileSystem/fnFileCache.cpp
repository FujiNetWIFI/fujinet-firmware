#include "fnFileCache.h"

#ifndef FNIO_IS_STDIO

#include <cstring>

#include <iostream>
#include <bitset>
#include <string>
#include <algorithm>

// TODO: Replace MD5 with some simple non-crypto hash function producing 128+ bit hash ...
#include <mbedtls/version.h>
#include <mbedtls/md5.h>

#include "../../include/debug.h"

#ifdef ESP_PLATFORM
#include <sys/time.h>
#else
#include "compat_gettimeofday.h"
#endif

#include "fnFileMem.h"
#include "fnFsSD.h"


// Directory on SD card used as file cache
#define FILE_CACHE_DIRECTORY    "/FujiNet/cache"
// Files in SD cache older than this value (second since mtime) are considered as expired
#define CACHE_FILE_MAX_AGE      10800
// Files over this size are changed from in memory to SD
#define DEFAULT_PERSISTENT_THRESHOLD  204800
#define COPY_BLK_SIZE           4096


static const std::string BASE32_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static std::string encode_base32(const std::string& data) 
{
    std::string binary_string;
    for (char c : data) 
    {
        binary_string += std::bitset<8>(c).to_string();
    }
    int padding = binary_string.length() % 5;
    if (padding != 0) binary_string.append(5 - padding, '0');
    std::string encoded;
    for (size_t i = 0; i < binary_string.length(); i += 5) 
    {
        encoded += BASE32_ALPHABET[std::stoi(binary_string.substr(i, 5), nullptr, 2)];
    }
    while (encoded.length() % 8 != 0) encoded += '=';
    return encoded;
}

/**
 * @brief Encode host and path into string suitable for file name
 */
static std::string encode_host_path(const char *host, const char *path)
{
    unsigned char md5_result[16];
    std::string result;
    // host part
#if MBEDTLS_VERSION_NUMBER >= 0x02070000 && MBEDTLS_VERSION_NUMBER < 0x03000000
    int err = mbedtls_md5_ret((const unsigned char *)host, strlen(host), md5_result);
    if (err != 0) {
        Debug_printf("mbedtls_md5_ret failed with error code %d\n", err);
    }
#else
    mbedtls_md5((const unsigned char *)host, strlen(host), md5_result);
#endif
    result = encode_base32(std::string((char *)md5_result, 5)) + '-';
    // path part
#if MBEDTLS_VERSION_NUMBER >= 0x02070000 && MBEDTLS_VERSION_NUMBER < 0x03000000
    err = mbedtls_md5_ret((const unsigned char *)path, strlen(path), md5_result);
    if (err != 0) {
        Debug_printf("mbedtls_md5_ret failed with error code %d\n", err);
    }
#else
    mbedtls_md5((const unsigned char *)path, strlen(path), md5_result);
#endif
    result += encode_base32(std::string((char *)md5_result, 15));
    return result;
}

static std::string get_file_path(const std::string &name)
{
    return std::string(FILE_CACHE_DIRECTORY) + '/' + name;
}

FileHandler *FileCache::open(const char *host, const char *path, const char *mode)
{
    FileHandler *fh = nullptr;

    if (!fnSDFAT.running())
        return nullptr;

    std::string cache_path(get_file_path(encode_host_path(host, path)));

    // test file age, do not use old/expired
    struct timeval now;
#ifdef ESP_PLATFORM
    gettimeofday(&now, nullptr);
#else
	compat_gettimeofday(&now, nullptr);
#endif
    if (now.tv_sec - fnSDFAT.mtime(cache_path.c_str()) < CACHE_FILE_MAX_AGE)
    {
        // open SD file
        fh = fnSDFAT.filehandler_open(cache_path.c_str(), mode);
    }

    if (fh != nullptr)
    {
        // Cache hit
        Debug_printf("Using SD cache file: %s\n", cache_path.c_str());
    }

    return fh;
}

fc_handle *FileCache::create(const char *host, const char *path, int threshold, int max_size)
{
    fc_handle *fc;

    // Create memory cache file
    FileHandler *fh = new FileHandlerMem;
    if (fh == nullptr)
    {
        Debug_println("FileCache::create - failed to open memory file");
        return nullptr;
    }

    // Prepare file cache handle
    fc = new fc_handle;
    if (fc == nullptr)
    {
        Debug_println("FileCache::create - failed to get fc_handle");
        fh->close();
        return nullptr;
    }
    fc->fh = fh;
    fc->threshold = (threshold < 0) ? DEFAULT_PERSISTENT_THRESHOLD : threshold;
    fc->max_size = max_size;
    fc->size = 0;
    fc->persistent = false;
    fc->host = std::string(host);
    fc->path = std::string(path);
    fc->name = encode_host_path(host, path);

    return fc;
}

size_t FileCache::write(fc_handle *fc, const void *data, size_t len)
{
    if (fc == nullptr || fc->fh == nullptr)
        return 0;

    // Write cache file
    size_t write_len = len;
    if (fc->max_size >= 0 && fc->max_size - fc->size < len)
    {
        // limit to max_size
        write_len = fc->max_size - fc->size;
    }
    size_t result = fc->fh->write(data, 1, write_len);
    fc->size += result;

    // check if memory file is over limit
    if (!fc->persistent && fc->size >= fc->threshold)
    {
        // Switch from memory to SD card
        if (!fnSDFAT.running())
        {
            Debug_println("FileCache::write - SD Filesystem is not running");
            return result;
        }

        Debug_printf("Writing SD cache file: %s\n", get_file_path(fc->name).c_str());

        // Ensure cache directory exists
        fnSDFAT.create_path(FILE_CACHE_DIRECTORY);

        // Open SD file
        FileHandler *fh_sd = fnSDFAT.filehandler_open(get_file_path(fc->name).c_str(), "wb+");
        if (fh_sd == nullptr)
        {
            Debug_println("FileCache::write - failed to open SD file");
            return result;
        }

        // Copy from memory to SD file
        //Debug_println("Changing memory file to SD file");
        size_t count = 0;
        uint8_t *buf = (uint8_t *)malloc(COPY_BLK_SIZE);
        fc->fh->seek(0, SEEK_SET);
        do
        {
            count = fc->fh->read(buf, 1, COPY_BLK_SIZE);
            fh_sd->write(buf, 1, count);
        } while (count > 0);
        free(buf);
        // Update handle
        fc->fh->close();
        fc->fh = fh_sd;
        fc->persistent = true;
        // Write some info into file (optional, not used by anything)
        fh_sd = fnSDFAT.filehandler_open((get_file_path(fc->name) + ".TXT").c_str(), "wb+");
        std::string info("Host: "+ fc->host +"\r\nFile: "+ fc->path+ "\r\nCache: "+ fc->name +"\r\n");
        fh_sd->write(info.c_str(), 1, info.size());
        fh_sd->close();
        //Debug_println("Changed to SD");
    }
    return result;
}


FileHandler *FileCache::reopen(fc_handle *fc, const char *mode)
{
    FileHandler *fh;

    if (fc == nullptr || fc->fh == nullptr)
        return nullptr;

    if (fc->persistent)
    {
        // reopen SD cache file
        fc->fh->flush();
        fc->fh->close();
        fh = fnSDFAT.filehandler_open(get_file_path(fc->name).c_str(), mode);
    }
    else
    {
        // rewind memory cache file
        fc->fh->seek(0, SEEK_SET);
        fh = fc->fh;
    }
    delete fc;
    return fh;
}


void FileCache::remove(fc_handle *fc)
{
    if (fc == nullptr || fc->fh == nullptr)
        return;

    fc->fh->close();
    if (fc->persistent)
    {
        // remove SD cache file
        fnSDFAT.remove(get_file_path(fc->name).c_str());
        fnSDFAT.remove((get_file_path(fc->name) + ".TXT").c_str());
    }
    delete fc;
}

#endif //!FNIO_IS_STDIO
