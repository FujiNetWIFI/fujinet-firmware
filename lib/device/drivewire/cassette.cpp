#ifdef BUILD_COCO

#include "cassette.h"

#include <cstring>

#ifdef ESP_PLATFORM
#ifndef CONFIG_IDF_TARGET_ESP32S3
#include <driver/dac_oneshot.h>
#endif
#endif

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFsSD.h"
#include "fnFsSPIFFS.h"

#include "led.h"

#define SAMPLE_DELAY_US 89

/**
 * @brief Handle when motor active, and send tape via DAC
 * @note This routine stays active until tape is done streaming.
 */
void drivewireCassette::play()
{
    // FILE *casf = fsFlash.file_open("/hdbcc2.raw", "r");
    // uint8_t *casbuf = NULL;
    // size_t sz = 0UL;

    // Debug_printv("Reading /hdbcc2.raw");
    
    // // Determine and allocate memory for image
    // fseek(casf, 0UL, SEEK_END);
    // sz = ftell(casf);
    // casbuf = (uint8_t *)malloc(sz);
    // fseek(casf, 0UL, SEEK_SET);

    // // Read image into memory
    // fread(casbuf, sizeof(uint8_t), sz, casf);
    
    // // Done with file
    // fclose(casf);

    // // Now play it back.
    // Debug_printv("Enabling DAC.")
    //     dac_output_enable(DAC_CHANNEL_1);

    // Debug_printv("sending data.");

    // for (size_t i = 0; i < sz; i++)
    // {
    //     dac_output_voltage(DAC_CHANNEL_1, casbuf[i]);

    //     // Abort if motor drops.
    //     if (!DRIVEWIRE.motorActive)
    //         break;

    //     esp_rom_delay_us(SAMPLE_DELAY_US);
    // }

    // Debug_printv("Disabling DAC.");

    // dac_output_disable(DAC_CHANNEL_1);

    // // Deallocate casbuf.
    // if (casbuf)
    // {
    //     free(casbuf);
    //     casbuf = NULL;
    // }

    // Debug_printv("Tape done.");
}

void drivewireCassette::setup()
{
}

void drivewireCassette::shutdown()
{
}

#endif /* BUILD_COCO */
