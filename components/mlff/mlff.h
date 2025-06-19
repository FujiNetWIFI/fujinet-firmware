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

#ifndef MLFF_H
#define MLFF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"

#define TAG "MLFF"
#define BUFFER_SIZE 4096
#define SD_MOUNT_POINT "/sd"
#define FIRMWARE_PATH "/sd/.bin"
#define PROGRESS_INTERVAL 10 // Log progress every 10%
#define MAX_PATH_LEN 1024
#define MAX_NAME_LEN 32
#define NVS_NAMESPACE "system"
#define NVS_UPDATE_KEY "update"

typedef struct {
    uint8_t gpio_miso;
    uint8_t gpio_mosi;
    uint8_t gpio_sck;
    uint8_t gpio_cs;
} sdcard_pins_t;

// static esp_err_t clear_nvs_update(void);
// static esp_err_t mount_sdcard();
static void get_partition_name_from_path(const char *path, char *out_name, size_t max_len);
static esp_err_t get_file_sha256(FILE *file, size_t file_size, unsigned char *sha256, bool compare_stored);
static esp_err_t flash_write_file(const char *sd_file_path, const char *partition_name);
void mlff_update(uint8_t sd_cs, uint8_t sd_miso, uint8_t sd_mosi, uint8_t sd_sck);

#ifdef __cplusplus
}
#endif

#endif // MLFF_H