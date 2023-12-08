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
uint8_t *casbuf = NULL;

static void _play(void* arg)
{
    drivewireCassette *cass = (drivewireCassette *)arg;
    
    casf = fsFlash.file_open("/hdbcc2.raw","r");

    Debug_printf("PLAY\n");

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

    size_t sz=0UL;

    fseek(casf,0UL,SEEK_END);
    sz = ftell(casf);
    casbuf = (uint8_t *)malloc(sz);
    fseek(casf,0UL,SEEK_SET);

    fread(casbuf,sizeof(uint8_t),sz,casf);

    if (casf)
    {
        fclose(casf);
        casf = NULL;
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

    for (size_t i=0;i<sz;i++)
    {
        dac_output_voltage(DAC_CHANNEL_1,casbuf[i]);
        esp_rom_delay_us(SAMPLE_DELAY_US);
    }

    Debug_printv("Disabling DAC.");

    dac_output_disable(DAC_CHANNEL_1);

    if (casbuf)
        free(casbuf);

    Debug_printv("Tape done.");
    TaskHandle_t t = cass->playTask;
    cass->playTask=NULL;
    vTaskDelete(t);
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
    xTaskCreate(_play,"playTask",4096,this,8,&playTask);
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

        if (casbuf)
        {
            free(casbuf);
            casbuf=NULL;
        }
    }
}

void drivewireCassette::setup()
{
}

void drivewireCassette::shutdown()
{
}

#endif /* BUILD_COCO */