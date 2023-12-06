#ifdef BUILD_COCO

#include "cassette.h"

#include <cstring>
#include <driver/dac.h>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnFsSD.h"
#include "fnFsSPIFFS.h"

#include "led.h"

#define SAMPLE_DELAY_US 89

FILE *casf = NULL;

static void _play(void* arg)
{
    drivewireCassette *cass = (drivewireCassette *)arg;
    
    casf = fsFlash.file_open("/hdbcc2.raw","r");

    if (!casf)
    {
        Debug_printv("Could not open cassette file. Aborting.");
        casf = NULL;
        return;
    }
    else
    {
        Debug_printv("cassette file opened.");
    }

    Debug_printv("Enabling DAC.")
    dac_output_enable(DAC_CHANNEL_1);

    // Send silence
    Debug_printv("sending silence");
    for (unsigned long i=0;i<1000000UL;i++)
        {
            dac_output_voltage(DAC_CHANNEL_1,0);
            esp_rom_delay_us(5);        
        }

    Debug_printv("sending data.");

    while (!feof(casf))
    {
        uint8_t b = fgetc(casf);
        dac_output_voltage(DAC_CHANNEL_1,b);
        esp_rom_delay_us(SAMPLE_DELAY_US);
    }

    Debug_printv("Disabling DAC.");

    dac_output_disable(DAC_CHANNEL_1);

    fclose(casf);
    casf = NULL;

    Debug_printv("Tape done.");
 
    // We're done, just wait to be killed.
    while(1)
        vTaskDelay(10/portTICK_PERIOD_MS);
}

/**
 * @brief since cassette isn't a DW device, we don't handle it here.
 */
void drivewireCassette::drivewire_process(uint32_t commanddata, uint8_t checksum)
{
    // Not really used...
}

/**
 * @brief Handle when motor active, and send tape via DAC
 * @note This routine stays active until tape is done streaming.
 */
void drivewireCassette::play()
{
    if (playTask)
        return;

    Debug_printv("Play tape");    
    xTaskCreate(_play,"playTask",4096,this,20,&playTask);
}

/**
 * @brief Handle when motor inactive, stop task if needed
 */
void drivewireCassette::stop()
{
    if (playTask)
    {
        Debug_printv("Stop tape");
        
        if (casf)
        {
            fclose(casf);
        }
        
        vTaskDelete(playTask);
        playTask=NULL;
    }
}

void drivewireCassette::setup()
{
}

void drivewireCassette::shutdown()
{
}

#endif /* BUILD_COCO */