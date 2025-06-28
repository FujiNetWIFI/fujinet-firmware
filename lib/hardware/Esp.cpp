/*
 Esp.cpp - ESP31B-specific APIs
 Copyright (c) 2015 Ivan Grokhotkov. All rights reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "Esp.h"

#include <esp_partition.h>
#include <soc/soc.h>
#include <soc/efuse_reg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/temperature_sensor.h"

#include <memory>

#include "esp_sleep.h"
#include "spi_flash_mmap.h"
extern "C" {
#include "esp_image_format.h"
#include "esp_ota_ops.h"
}
// #include <MD5Builder.h>

#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "soc/spi_reg.h"
#ifdef ESP_IDF_VERSION_MAJOR  // IDF 4+
#if CONFIG_IDF_TARGET_ESP32   // ESP32/PICO-D4
#include "esp32/rom/spi_flash.h"
#include "soc/efuse_reg.h"
#define ESP_FLASH_IMAGE_BASE \
    0x1000  // Flash offset containing flash size and spi mode
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/spi_flash.h"
#include "soc/efuse_reg.h"
#define ESP_FLASH_IMAGE_BASE 0x1000
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/spi_flash.h"
#include "soc/efuse_reg.h"
#define ESP_FLASH_IMAGE_BASE 0x0000  // Esp32s3 is located at 0x0000
#elif CONFIG_IDF_TARGET_ESP32C2
#include "esp32c2/rom/spi_flash.h"
#define ESP_FLASH_IMAGE_BASE 0x0000  // Esp32c2 is located at 0x0000
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/spi_flash.h"
#define ESP_FLASH_IMAGE_BASE 0x0000  // Esp32c3 is located at 0x0000
#elif CONFIG_IDF_TARGET_ESP32C6
#include "esp32c6/rom/spi_flash.h"
#define ESP_FLASH_IMAGE_BASE 0x0000  // Esp32c6 is located at 0x0000
#elif CONFIG_IDF_TARGET_ESP32H2
#include "esp32h2/rom/spi_flash.h"
#define ESP_FLASH_IMAGE_BASE 0x0000  // Esp32h2 is located at 0x0000
#else
#error Target CONFIG_IDF_TARGET is not supported
#endif
#else  // ESP32 Before IDF 4.0
#include "rom/spi_flash.h"
#define ESP_FLASH_IMAGE_BASE 0x1000
#endif

// REG_SPI_BASE is not defined for S3/C3 ??

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#ifdef REG_SPI_BASE
#undef REG_SPI_BASE
#endif  // REG_SPI_BASE
#define REG_SPI_BASE(i) \
    (DR_REG_SPI1_BASE + \
     (((i) > 1) ? (((i) * 0x1000) + 0x20000) : (((~(i)) & 1) * 0x1000)))
#endif  // TARGET

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

/**
 * User-defined Literals
 *  usage:
 *
 *   uint32_t = test = 10_MHz; // --> 10000000
 */

unsigned long long operator"" _kHz(unsigned long long x) { return x * 1000; }

unsigned long long operator"" _MHz(unsigned long long x) {
    return x * 1000 * 1000;
}

unsigned long long operator"" _GHz(unsigned long long x) {
    return x * 1000 * 1000 * 1000;
}

unsigned long long operator"" _kBit(unsigned long long x) { return x * 1024; }

unsigned long long operator"" _MBit(unsigned long long x) {
    return x * 1024 * 1024;
}

unsigned long long operator"" _GBit(unsigned long long x) {
    return x * 1024 * 1024 * 1024;
}

unsigned long long operator"" _kB(unsigned long long x) { return x * 1024; }

unsigned long long operator"" _MB(unsigned long long x) {
    return x * 1024 * 1024;
}

unsigned long long operator"" _GB(unsigned long long x) {
    return x * 1024 * 1024 * 1024;
}

void EspClass::deepSleep(uint64_t time_us) { esp_deep_sleep(time_us); }

void EspClass::restart(void) { esp_restart(); }

uint32_t EspClass::getHeapSize(void) {
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
}

uint32_t EspClass::getFreeHeap(void) {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t EspClass::getMinFreeHeap(void) {
    return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t EspClass::getMaxAllocHeap(void) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

uint32_t EspClass::getPsramSize(void) {
#ifdef BOARD_HAS_PSRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    }
#endif
    return 0;
}

uint32_t EspClass::getFreePsram(void) {
#ifdef BOARD_HAS_PSRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
#endif
    return 0;
}

uint32_t EspClass::getMinFreePsram(void) {
#ifdef BOARD_HAS_PSRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    }
#endif
    return 0;
}

uint32_t EspClass::getMaxAllocPsram(void) {
#ifdef BOARD_HAS_PSRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
#endif
    return 0;
}

uint16_t EspClass::getChipRevision(void) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return chip_info.revision;
}

const char *EspClass::getChipModel(void) {
#if CONFIG_IDF_TARGET_ESP32
    uint32_t chip_ver =
        //REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_VER_PKG);
        REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_PACKAGE);
    uint32_t pkg_ver = chip_ver & 0x7;
    switch (pkg_ver) {
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6:
            if ((getChipRevision() / 100) == 3) {
                return "ESP32-D0WDQ6-V3";
            } else {
                return "ESP32-D0WDQ6";
            }
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5:
            if ((getChipRevision() / 100) == 3) {
                return "ESP32-D0WD-V3";
            } else {
                return "ESP32-D0WD";
            }
        case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5:
            return "ESP32-D2WD";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2:
            return "ESP32-PICO-D2";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:
            return "ESP32-PICO-D4";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302:
            return "ESP32-PICO-V3-02";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3:
            return "ESP32-D0WDR2-V3";
        default:
            return "Unknown";
    }
#elif CONFIG_IDF_TARGET_ESP32S2
    uint32_t pkg_ver =
        REG_GET_FIELD(EFUSE_RD_MAC_SPI_SYS_3_REG, EFUSE_PKG_VERSION);
    switch (pkg_ver) {
        case 0:
            return "ESP32-S2";
        case 1:
            return "ESP32-S2FH16";
        case 2:
            return "ESP32-S2FH32";
        default:
            return "ESP32-S2 (Unknown)";
    }
#else
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    switch (chip_info.model) {
        case CHIP_ESP32S3:
            return "ESP32-S3";
        case CHIP_ESP32C2:
            return "ESP32-C2";
        case CHIP_ESP32C3:
            return "ESP32-C3";
        // case CHIP_ESP32C6:
        //     return "ESP32-C6";
        case CHIP_ESP32H2:
            return "ESP32-H2";
        default:
            return "UNKNOWN";
    }
#endif
}

uint8_t EspClass::getChipCores(void) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return chip_info.cores;
}

const char *EspClass::getSdkVersion(void) { return esp_get_idf_version(); }

//const char *EspClass::getCoreVersion(void) { return ESP_ARDUINO_VERSION_STR; }

uint32_t ESP_getFlashChipId(void) {
    uint32_t id = g_rom_flashchip.device_id;
    id = ((id & 0xff) << 16) | ((id >> 16) & 0xff) | (id & 0xff00);
    return id;
}

uint32_t EspClass::getFlashChipSize(void) {
    uint32_t id = (ESP_getFlashChipId() >> 16) & 0xFF;
    return 2 << (id - 1);
}

uint32_t EspClass::getFlashChipSpeed(void) {
    esp_image_header_t fhdr;
    if (esp_flash_read(esp_flash_default_chip, (void *)&fhdr,
                       ESP_FLASH_IMAGE_BASE, sizeof(esp_image_header_t)) &&
        fhdr.magic != ESP_IMAGE_HEADER_MAGIC) {
        return 0;
    }
    return magicFlashChipSpeed(fhdr.spi_speed);
}

FlashMode_t EspClass::getFlashChipMode(void) {
#if CONFIG_IDF_TARGET_ESP32S2
    uint32_t spi_ctrl = REG_READ(PERIPHS_SPI_FLASH_CTRL);
#else
#if CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32C2 || \
    CONFIG_IDF_TARGET_ESP32C6
    uint32_t spi_ctrl = REG_READ(DR_REG_SPI0_BASE + 0x8);
#else
    uint32_t spi_ctrl = REG_READ(SPI_CTRL_REG(0));
#endif
#endif
    /* Not all of the following constants are already defined in older versions
     * of spi_reg.h, so do it manually for now*/
    if (spi_ctrl & BIT(24)) {  // SPI_FREAD_QIO
        return (FM_QIO);
    } else if (spi_ctrl & BIT(20)) {  // SPI_FREAD_QUAD
        return (FM_QOUT);
    } else if (spi_ctrl & BIT(23)) {  // SPI_FREAD_DIO
        return (FM_DIO);
    } else if (spi_ctrl & BIT(14)) {  // SPI_FREAD_DUAL
        return (FM_DOUT);
    } else if (spi_ctrl & BIT(13)) {  // SPI_FASTRD_MODE
        return (FM_FAST_READ);
    } else {
        return (FM_SLOW_READ);
    }
    return (FM_DOUT);
}

uint32_t EspClass::magicFlashChipSize(uint8_t byte) {
    /*
      FLASH_SIZES = {
          "1MB": 0x00,
          "2MB": 0x10,
          "4MB": 0x20,
          "8MB": 0x30,
          "16MB": 0x40,
          "32MB": 0x50,
          "64MB": 0x60,
          "128MB": 0x70,
      }
  */
    switch (byte & 0x0F) {
        case 0x0:
            return (1_MB);  // 8 MBit (1MB)
        case 0x1:
            return (2_MB);  // 16 MBit (2MB)
        case 0x2:
            return (4_MB);  // 32 MBit (4MB)
        case 0x3:
            return (8_MB);  // 64 MBit (8MB)
        case 0x4:
            return (16_MB);  // 128 MBit (16MB)
        case 0x5:
            return (32_MB);  // 256 MBit (32MB)
        case 0x6:
            return (64_MB);  // 512 MBit (64MB)
        case 0x7:
            return (128_MB);  // 1 GBit (128MB)
        default:              // fail?
            return 0;
    }
}

uint32_t EspClass::magicFlashChipSpeed(uint8_t byte) {
#if CONFIG_IDF_TARGET_ESP32C2
    /*
      FLASH_FREQUENCY = {
          "60m": 0xF,
          "30m": 0x0,
          "20m": 0x1,
          "15m": 0x2,
      }
  */
    switch (byte & 0x0F) {
        case 0xF:
            return (60_MHz);
        case 0x0:
            return (30_MHz);
        case 0x1:
            return (20_MHz);
        case 0x2:
            return (15_MHz);
        default:  // fail?
            return 0;
    }

#elif CONFIG_IDF_TARGET_ESP32C6
    /*
     FLASH_FREQUENCY = {
          "80m": 0x0,  # workaround for wrong mspi HS div value in ROM
          "40m": 0x0,
          "20m": 0x2,
      }
  */
    switch (byte & 0x0F) {
        case 0x0:
            return (80_MHz);
        case 0x2:
            return (20_MHz);
        default:  // fail?
            return 0;
    }

#elif CONFIG_IDF_TARGET_ESP32H2

    /*
      FLASH_FREQUENCY = {
          "48m": 0xF,
          "24m": 0x0,
          "16m": 0x1,
          "12m": 0x2,
      }
  */
    switch (byte & 0x0F) {
        case 0xF:
            return (48_MHz);
        case 0x0:
            return (24_MHz);
        case 0x1:
            return (16_MHz);
        case 0x2:
            return (12_MHz);
        default:  // fail?
            return 0;
    }

#else
    /*
      FLASH_FREQUENCY = {
          "80m": 0xF,
          "40m": 0x0,
          "26m": 0x1,
          "20m": 0x2,
      }
  */
    switch (byte & 0x0F) {
        case 0xF:
            return (80_MHz);
        case 0x0:
            return (40_MHz);
        case 0x1:
            return (26_MHz);
        case 0x2:
            return (20_MHz);
        default:  // fail?
            return 0;
    }
#endif
}

FlashMode_t EspClass::magicFlashChipMode(uint8_t byte) {
    FlashMode_t mode = (FlashMode_t)byte;
    if (mode > FM_SLOW_READ) {
        mode = FM_UNKNOWN;
    }
    return mode;
}

bool EspClass::flashEraseSector(uint32_t sector) {
    return esp_flash_erase_region(esp_flash_default_chip,
                                  sector * SPI_FLASH_SEC_SIZE,
                                  SPI_FLASH_SEC_SIZE) == ESP_OK;
}

// Warning: These functions do not work with encrypted flash
bool EspClass::flashWrite(uint32_t offset, uint32_t *data, size_t size) {
    return esp_flash_write(esp_flash_default_chip, (const void *)data, offset,
                           size) == ESP_OK;
}

bool EspClass::flashRead(uint32_t offset, uint32_t *data, size_t size) {
    return esp_flash_read(esp_flash_default_chip, (void *)data, offset, size) ==
           ESP_OK;
}

bool EspClass::partitionEraseRange(const esp_partition_t *partition,
                                   uint32_t offset, size_t size) {
    return esp_partition_erase_range(partition, offset, size) == ESP_OK;
}

bool EspClass::partitionWrite(const esp_partition_t *partition, uint32_t offset,
                              uint32_t *data, size_t size) {
    return esp_partition_write(partition, offset, data, size) == ESP_OK;
}

bool EspClass::partitionRead(const esp_partition_t *partition, uint32_t offset,
                             uint32_t *data, size_t size) {
    return esp_partition_read(partition, offset, data, size) == ESP_OK;
}

uint64_t EspClass::getEfuseMac(void) {
    uint64_t _chipmacid = 0LL;
    esp_efuse_mac_get_default((uint8_t *)(&_chipmacid));
    return _chipmacid;
}

#ifdef CONFIG_IDF_TARGET_ESP32

float EspClass::temperatureRead() {
    // temperature_sensor_handle_t temp_handle = NULL;
    // temperature_sensor_config_t temp_sensor = {
    //     .range_min = 20,
    //     .range_max = 50,
    // };
    // ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor, &temp_handle));

    // // Enable temperature sensor
    // ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
    // // Get converted sensor data
    // float tsens_out;
    // ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &tsens_out));

    // // Disable the temperature sensor if it is not needed and save the power
    // ESP_ERROR_CHECK(temperature_sensor_disable(temp_handle));

    // // Uninstall the temperature sensor
    // ESP_ERROR_CHECK(temperature_sensor_uninstall(temp_handle));

    // printf("Temperature in %f °C\r\n", tsens_out);

    // // return tsens_out; // Celsius
    // return (tsens_out - 32) / 1.8; // Fahrenheit
    return 0;
}

#elif SOC_TEMP_SENSOR_SUPPORTED

float EspClass::temperatureRead() {
    // float result = NAN;

    // static temperature_sensor_handle_t temp_sensor = NULL;
    // static volatile bool initialized = false;
    // if (!initialized) {
    //     initialized = true;
    //     // Install temperature sensor, expected temp ranger range: 10~50 ℃
    //     temperature_sensor_config_t temp_sensor_config =
    //         TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    //     if (temperature_sensor_install(&temp_sensor_config, &temp_sensor) !=
    //         ESP_OK) {
    //         initialized = false;
    //         temp_sensor = NULL;
    //         log_e("temperature_sensor_install failed");
    //     } else if (temperature_sensor_enable(temp_sensor) != ESP_OK) {
    //         temperature_sensor_uninstall(temp_sensor);
    //         initialized = false;
    //         temp_sensor = NULL;
    //         log_e("temperature_sensor_enable failed");
    //     }
    // }

    // if (initialized) {
    //     if (temperature_sensor_get_celsius(temp_sensor, &result) != ESP_OK) {
    //         log_e("temperature_sensor_get_celsius failed");
    //     }
    // }
    // return result;
    return 0;
}
#endif