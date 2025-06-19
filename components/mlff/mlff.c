// Meatloaf File Flasher (MLFF)
// https://github.com/idolpx/mlff
// Copyright(C) 2025 James Johnston
//
// MLFF is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// MLFF is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with MLFF. If not, see <http://www.gnu.org/licenses/>.

#include "mlff.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

//#include "esp_err.h"
//#include "esp_log.h"
//#include "esp_log_color.h"
#include "esp_vfs_fat.h"
//#include "driver/sdspi_host.h"
//#include "driver/spi_common.h"
//#include "sdmmc_cmd.h"
//#include "esp_flash.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "mbedtls/sha256.h"

// nvs_handle_t _nvs_handle;
// sdmmc_card_t *sdcard_info = NULL;

// Clear NVS update key
// static esp_err_t clear_nvs_update(void) {
//     uint8_t value;
//     esp_err_t ret = nvs_get_u8(_nvs_handle, NVS_UPDATE_KEY, &value);
//     if (value == 0) {
//         ESP_LOGE(TAG, "Already cleared");
//         return ret;
//     }

//     ret = nvs_set_u8(_nvs_handle, NVS_UPDATE_KEY, 0);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to set NVS update key: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     ret = nvs_commit(_nvs_handle);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
//     }

//     return ret;
// }

// static esp_err_t mount_sdcard() {
//     sdcard_pins_t sdcard_pins;
//     size_t length = sizeof(sdcard_pins);
//     esp_err_t ret = nvs_get_blob(_nvs_handle, "sdcard_pins", &sdcard_pins, &length);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to get sdcard_pins from NVS: %s", esp_err_to_name(ret));
//         return ret; 
//     }
//     ESP_LOGW(TAG, "cs[%d] mosi[%d] miso[%d] sck[%d]", sdcard_pins.gpio_cs, sdcard_pins.gpio_mosi, sdcard_pins.gpio_miso, sdcard_pins.gpio_sck);

//     const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
//         .format_if_mount_failed = false,
//         .max_files = 5,
//         .allocation_unit_size = 16 * 1024
//     };

//     // Set up a configuration to the SD host interface
//     sdmmc_host_t host_config = SDSPI_HOST_DEFAULT(); 

//     sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
//     slot_config.gpio_cs = sdcard_pins.gpio_cs;
//     slot_config.host_id = SDSPI_DEFAULT_HOST;

//     spi_bus_config_t bus_cfg = {
//         .mosi_io_num = sdcard_pins.gpio_mosi,
//         .miso_io_num = sdcard_pins.gpio_miso,
//         .sclk_io_num = sdcard_pins.gpio_sck,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//         .max_transfer_sz = 4000,
//     };

//     ret = spi_bus_initialize(SDSPI_DEFAULT_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     return esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host_config, &slot_config, &mount_config, &sdcard_info);
// }

// Extract filename without path and up to first '.'
static void get_partition_name_from_path(const char *path, char *out_name, size_t max_len) {
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path; // Skip path
    const char *dot = strchr(filename, '.');
    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (len >= max_len) len = max_len - 1;
    strncpy(out_name, filename, len);
    out_name[len] = '\0';
}

// Convert SHA256 hash to string
static void sha256_to_string(unsigned char *sha256, char *out_str) {
    for (int i = 0; i < 32; i++) {
        sprintf(out_str + i * 2, "%02x", sha256[i]);
    }
}

// Get SHA256 hash of file
static esp_err_t get_file_sha256(FILE *file, size_t file_size, unsigned char *sha256, bool compare_stored) {
    esp_err_t ret = ESP_OK;
    uint8_t *buffer = NULL;
    uint8_t stored_sha256[32];
    mbedtls_sha256_context sha256_ctx;

    if (compare_stored)
        file_size -= 32;

    // Allocate buffer
    buffer = (uint8_t *)malloc(BUFFER_SIZE);
    if (!buffer) {
        printf("Failed to allocate buffer\r\n");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // Initialize SHA256 context
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); // 0 for SHA256

    // Update SHA256 context with file data
    rewind(file);
    size_t total_read = 0;
    for (size_t offset = 0; offset < file_size; offset += BUFFER_SIZE) {
        size_t bytes_to_read = file_size - offset;
        if (bytes_to_read > BUFFER_SIZE) bytes_to_read = BUFFER_SIZE;
        size_t bytes_read = fread(buffer, 1, bytes_to_read, file);
        if (bytes_read != bytes_to_read) {
            printf("Failed to read file data: expected %zu, read %zu\r\n", bytes_to_read, bytes_read);
            ret = ESP_FAIL;
            goto cleanup;
        }
        mbedtls_sha256_update(&sha256_ctx, buffer, bytes_read);
        total_read += bytes_read;
    }

    // Get SHA256 hash
    mbedtls_sha256_finish(&sha256_ctx, sha256);
    mbedtls_sha256_free(&sha256_ctx);

    char sha256_str[65];
    sha256_to_string(sha256, sha256_str);
    printf("FILE CALCULATED SHA256: %s\r\n", sha256_str);

    // Read last 32 bytes for stored SHA256
    if (compare_stored)
    {
        size_t bytes_read = fread(stored_sha256, 1, 32, file);
        if (bytes_read != 32) {
            printf("Failed to read stored SHA256: expected 32, read %zu\r\n", bytes_read);
            ret = ESP_FAIL;
            goto cleanup;
        }
        sha256_to_string(stored_sha256, sha256_str);
        printf("FILE STORED SHA256    : %s\r\n", sha256_str);

        // Compare SHA256 hashes
        if (memcmp(sha256, stored_sha256, 32) != 0) {
            printf("SHA256 hashes do not match\r\n");
            ret = ESP_FAIL;
        }
    }

cleanup:
    if (buffer) free(buffer);
    return ret;
}


// Write file from SD card to flash
static esp_err_t flash_write_file(const char *sd_file_path, const char *partition_name) {
    esp_err_t ret = ESP_OK;
    FILE *sd_file = NULL;
    uint8_t *buffer = NULL;
    const esp_partition_t *existing_part = NULL;
    esp_partition_t partition;
    unsigned char sd_sha256[32], flash_sha256[32];
    char sha256_str[65];

    // Open SD card file
    sd_file = fopen(sd_file_path, "rb");
    if (!sd_file) {
        printf("Failed to open SD file %s\r\n", sd_file_path);
        ret = ESP_FAIL;
        goto cleanup;
    }

    // Get file size
    fseek(sd_file, 0, SEEK_END);
    size_t file_size = ftell(sd_file);
    rewind(sd_file);

    existing_part = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (existing_part && strcasecmp(partition_name, "bootloader") == 0)
    {
        // Set bootloader partition
        partition = *existing_part;
        partition.address = 0x1000;
        partition.size = 0x7000;
        partition.erase_size = 0x1000;
        partition.type = ESP_PARTITION_TYPE_BOOTLOADER;
        partition.subtype = ESP_PARTITION_SUBTYPE_BOOTLOADER_PRIMARY;
        strcpy(partition.label, partition_name);
    }
    else if (existing_part && strcasecmp(partition_name, "partitions") == 0)
    {
        // Set partition table partition
        partition = *existing_part;
        partition.address = 0x8000;
        partition.size = 0x1000;
        partition.erase_size = 0x1000;
        partition.type = ESP_PARTITION_TYPE_PARTITION_TABLE;
        partition.subtype = ESP_PARTITION_SUBTYPE_PARTITION_TABLE_PRIMARY;
        strcpy(partition.label, partition_name);
    }
    else
    {
        // Find partition by name
        existing_part = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, partition_name);
        if (existing_part)
            partition = *existing_part;
    }

    if (!existing_part) {
        printf("Partition '%s' not found\r\n", partition_name);
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    // Partition details
    printf("Partition Label: %s\r\n", partition.label);
    printf("Partition Type: 0x%02x\r\n", partition.type);
    printf("Partition Subtype: 0x%02x\r\n", partition.subtype);
    printf("Partition Address: 0x%08lx\r\n", partition.address);
    printf("Partition Size: 0x%08lx\r\n", partition.size);
    printf("Partition Erase Size: 0x%08lx\r\n", partition.erase_size);

    // Validate file size against partition
    if (file_size > partition.size) {
        printf("File size (%zu) exceeds partition size (%lu) for %s\r\n", file_size, partition.size, partition_name);
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    // Compare File & Partition SHA256
    //ESP_LOGI(TAG, "Compare File & Partition SHA256...");
    if ( partition.type > ESP_PARTITION_TYPE_APP )
    {
        ret = get_file_sha256(sd_file, file_size, sd_sha256, false);
    } else {
        ret = get_file_sha256(sd_file, file_size, sd_sha256, true);
    }
    if (ret != ESP_OK) {
        printf("Failed to get file SHA256: %s\r\n", esp_err_to_name(ret));
        goto cleanup;
    }
    if ( partition.type == ESP_PARTITION_TYPE_APP )
    {
        ret = esp_partition_get_sha256(&partition, flash_sha256);
        if (ret != ESP_OK) {
            printf("Failed to get partition SHA256: %s\r\n", esp_err_to_name(ret));
            goto write_bin;
        }
        sha256_to_string(flash_sha256, sha256_str);
        printf("PARTITION SHA256      : %s\r\n", sha256_str);

        // Compare SHA256
        if (memcmp(sd_sha256, flash_sha256, 32) == 0) {
            printf("File & Partition SHA256 match. Skipping write...\r\n");
            goto cleanup;
        }
    }


write_bin:
    // Allocate buffer
    buffer = (uint8_t *)malloc(BUFFER_SIZE);
    if (!buffer) {
        printf("Failed to allocate buffer for %s\r\n", sd_file_path);
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // Erase partition
    printf("Erasing partition '%s'...\r\n", partition_name);
    // if ( partition.type < ESP_PARTITION_TYPE_BOOTLOADER )
    // {
         ret = esp_partition_erase_range(&partition, 0, partition.size);
    // } else {
    //    ret = esp_flash_erase_region(partition.flash_chip, partition.address, partition.size);
    //}
    if (ret != ESP_OK) {
        printf("Failed to erase partition %s: %s\r\n", partition_name, esp_err_to_name(ret));
        goto cleanup;
    }

    // Write and calculate SD MD5 with progress
    size_t total_written = 0;
    uint32_t last_progress = 0;
    rewind(sd_file);
    printf("Writing '%s' to partition '%s'\r\n", sd_file_path, partition_name);
    while (total_written < file_size) {
        size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, sd_file);
        if (bytes_read == 0) break;

        // if ( partition.type < ESP_PARTITION_TYPE_BOOTLOADER )
        // {
            ret = esp_partition_write(&partition, total_written, buffer, bytes_read);
            if (ret != ESP_OK) {
                printf("Failed to write to partition %s: %s\r\n", partition_name, esp_err_to_name(ret));
                goto cleanup;
            }
        // } else {
        //     ret = esp_flash_write(partition.flash_chip, buffer, partition.address + total_written, bytes_read);
        //     if (ret != ESP_OK) {
        //         printf("Failed to write to flash: %s\r\n", esp_err_to_name(ret));
        //         goto cleanup;
        //     }
        // }
        total_written += bytes_read;

        // Log progress
        uint32_t progress = (total_written * 100) / file_size;
        if (progress / PROGRESS_INTERVAL > last_progress) {
            printf("%lu%% (%zu/%zu bytes)\r\n", progress, total_written, file_size);
            last_progress = progress / PROGRESS_INTERVAL;
        }
    }

    if ( partition.type < ESP_PARTITION_TYPE_BOOTLOADER )
    {
        // Verify partition data with progress
        printf("Verifying partition '%s'...\r\n", partition_name);
        ret = esp_partition_get_sha256(&partition, flash_sha256);
        if (ret != ESP_OK) {
            printf("Failed to get partition SHA256: %s\r\n", esp_err_to_name(ret));
            goto cleanup;
        }
        sha256_to_string(flash_sha256, sha256_str);
        printf("PARTITION SHA256      : %s\r\n", sha256_str);
    } else {
        memcpy(flash_sha256, sd_sha256, 32);
    }

    // Compare SHA256
    if (memcmp(sd_sha256, flash_sha256, 32) != 0) {
        printf("Verification failed: SHA256 mismatch\r\n");
        ret = ESP_FAIL;
    } else {
        // Rename the .bin file on SD
        char *new_sd_file_path = (char *)malloc(strlen(sd_file_path) + 8);
        strcpy(new_sd_file_path, sd_file_path);
        strcat(new_sd_file_path, ".ok");
        if (rename(sd_file_path, new_sd_file_path) != 0) {
            printf("Failed to rename %s to %s\r\n", sd_file_path, new_sd_file_path);
            ret = ESP_FAIL;
            goto cleanup;
        }

        // // Delete the .bin file on SD
        // if (unlink(sd_file_path) != 0) {
        //     printf("Failed to delete %s from SD card", sd_file_path);
        //     ret = ESP_FAIL;
        //     goto cleanup;
        // }

        printf("Successfully wrote, verified '%s' (%zu bytes) to partition '%s'\r\n", sd_file_path, total_written, partition_name);
    }

cleanup:
    if (buffer) free(buffer);
    if (sd_file) fclose(sd_file);
    return ret;
}

void mlff_update(uint8_t sd_cs, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_sck) {
    nvs_handle_t _nvs_handle;
    sdcard_pins_t sdcard_pins_c = {0}, sdcard_pins_nvs = {0};
    size_t length = sizeof(sdcard_pins_c);

    sdcard_pins_c.gpio_cs = sd_cs;
    sdcard_pins_c.gpio_miso = sd_miso;
    sdcard_pins_c.gpio_mosi = sd_mosi;
    sdcard_pins_c.gpio_sck = sd_sck;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret == ESP_OK) {
            ret = nvs_flash_init();
        }
    }
    if (ret != ESP_OK) {
        printf("Failed to initialize NVS: %s\r\n", esp_err_to_name(ret));
        goto fail_exit;
    }

    // Open NVS
    printf("Opening NVS namespace...\r\n");
    ret = nvs_open((const char *)"system", NVS_READWRITE, &_nvs_handle);
    if (ret != ESP_OK) {
        printf("Failed to open NVS namespace: %s\r\n", esp_err_to_name(ret));
        goto fail_exit;
    }

    ret = nvs_set_u8(_nvs_handle, (const char *)"update", 1);
    if (ret != ESP_OK) {
        printf("Failed to set NVS update key: %s\r\n", esp_err_to_name(ret));
        goto fail_exit;
    }

    // Check to make sure SD card pins are set in NVS
    ret = nvs_get_blob(_nvs_handle, "sdcard_pins", &sdcard_pins_nvs, &length);
    if (ret != ESP_OK) {
        printf("Failed to get sdcard_pins from NVS: %s\r\n", esp_err_to_name(ret));
    }
    printf("CUR cs[%d] mosi[%d] miso[%d] sck[%d]\r\n", sdcard_pins_c.gpio_cs, sdcard_pins_c.gpio_mosi, sdcard_pins_c.gpio_miso, sdcard_pins_c.gpio_sck);
    printf("NVS cs[%d] mosi[%d] miso[%d] sck[%d]\r\n", sdcard_pins_nvs.gpio_cs, sdcard_pins_nvs.gpio_mosi, sdcard_pins_nvs.gpio_miso, sdcard_pins_nvs.gpio_sck);

    // Update SD card pins in NVS if they are different
    if (memcmp(&sdcard_pins_c, &sdcard_pins_nvs, sizeof(sdcard_pins_c)) != 0) {
        printf("sdcard_pins changed, writing to NVS...\r\n");
        ret = nvs_set_blob(_nvs_handle, "sdcard_pins", &sdcard_pins_c, sizeof(sdcard_pins_c));
        if (ret != ESP_OK) {
            printf("Failed to set sdcard_pins to NVS: %s\r\n", esp_err_to_name(ret));
            goto fail_exit;
        }
    }

    ret = nvs_commit(_nvs_handle);
    if (ret != ESP_OK) {
        printf("Failed to commit NVS: %s\r\n", esp_err_to_name(ret));
        goto fail_exit;
    }
    nvs_close(_nvs_handle);

    DIR *dir = opendir(FIRMWARE_PATH);
    if (!dir) {
        printf("Failed to open SD card directory %s\r\n", FIRMWARE_PATH);
        //esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sdcard_info);
        goto fail_exit;
    }

    struct dirent *entry;
    char file_path[MAX_PATH_LEN];
    char partition_name[MAX_NAME_LEN];
    int file_count = 0;

    printf("Searching for '.bin' files in '%s'\r\n", FIRMWARE_PATH);
    while ((entry = readdir(dir)) != NULL) {
        // Check if file ends with .bin (case-insensitive)
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && strcasecmp(ext, ".bin") == 0 && strncasecmp(entry->d_name, "main", 4) != 0) {
            snprintf(file_path, MAX_PATH_LEN, "%s/%s", FIRMWARE_PATH, entry->d_name);
            struct stat st;
            if (stat(file_path, &st) == 0 && S_ISREG(st.st_mode)) {
                get_partition_name_from_path(file_path, partition_name, MAX_NAME_LEN);
                printf("File found: '%s', using partition name: '%s'\r\n", file_path, partition_name);
                esp_err_t ret = flash_write_file(file_path, partition_name);
                if (ret == ESP_OK) {
                    printf("Successfully processed '%s'\r\n", file_path);
                    file_count++;
                } else {
                    printf("Failed to process '%s'\r\n", file_path);
                }
            }
        } else if (strlen(entry->d_name) > 0) {
            printf("Skipping '%s'\r\n", entry->d_name);
        }
    }
    closedir(dir);

    // ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sdcard_info);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to unmount SD card: '%s'", esp_err_to_name(ret));
    // }

    // if (file_count == 0) {
    //     ESP_LOGW(TAG, "No .bin files found on SD card");
    // } else {
    //     ESP_LOGI(TAG, "Processed %d .bin files", file_count);
    // }

fail_exit:

    // Set NVS update key to false
    // ESP_LOGI(TAG, "Clearing NVS update key");
    // ret = clear_nvs_update();
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to set NVS update key");
    // }

    //nvs_close(_nvs_handle);
    esp_restart();
}
