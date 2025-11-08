#ifdef BUILD_IEC

#include "iec.h"
#include "fujiDevice.h"

#include <cstring>
#include <memory>

#include "soc/io_mux_reg.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "../../hardware/led.h"

#define MAIN_STACKSIZE   32768
#define MAIN_PRIORITY    17
#define MAIN_CPUAFFINITY 1

systemBus::systemBus() : IECBusHandler(PIN_IEC_ATN, PIN_IEC_CLK_OUT, PIN_IEC_DATA_OUT,
                                       PIN_IEC_RESET==GPIO_NUM_NC ? 0xFF : PIN_IEC_RESET,
                                       0xFF,
                                       PIN_IEC_SRQ==GPIO_NUM_NC   ? 0xFF : PIN_IEC_SRQ)
{
#ifdef SUPPORT_DOLPHIN
#ifdef SUPPORT_DOLPHIN_XRA1405
  setDolphinDosPins(PIN_PARALLEL_FLAG2 == GPIO_NUM_NC ? 0xFF : PIN_PARALLEL_FLAG2,
                    PIN_PARALLEL_PC2   == GPIO_NUM_NC ? 0xFF : PIN_PARALLEL_PC2,
                    PIN_SD_HOST_SCK    == GPIO_NUM_NC ? 0xFF : PIN_SD_HOST_SCK,
                    PIN_SD_HOST_MOSI   == GPIO_NUM_NC ? 0xFF : PIN_SD_HOST_MOSI,
                    PIN_SD_HOST_MISO   == GPIO_NUM_NC ? 0xFF : PIN_SD_HOST_MISO,
                    PIN_XRA1405_CS     == GPIO_NUM_NC ? 0xFF : PIN_XRA1405_CS);
#else
#error "Can only support DolphinDos using XRA1405 port expander"
#endif
#endif
}

// static void ml_iec_intr_task(void* arg)
// {
//     while ( true )
//     {
//       IEC.service();
//       taskYIELD(); // Allow other tasks to run
//     }
// }

// void init_gpio(gpio_num_t _pin)
// {
//     PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[_pin], PIN_FUNC_GPIO);
//     gpio_set_direction(_pin, GPIO_MODE_INPUT);
//     gpio_pullup_en(_pin);
//     gpio_set_pull_mode(_pin, GPIO_PULLUP_ONLY);
//     gpio_set_level(_pin, 0);
//     return;
// }

void systemBus::setup()
{
  Debug_printf("IEC systemBus::setup()\r\n");
  begin();
#ifdef SUPPORT_JIFFY
  Debug_printf("JiffyDOS protocol supported\r\n");
#endif
#ifdef SUPPORT_EPYX
  Debug_printf("Epyx FastLoad protocol supported\r\n");
#endif
#ifdef SUPPORT_DOLPHIN
  Debug_printf("DolphinDOS protocol supported\r\n");
#endif

//     // initial pin modes in GPIO
//     init_gpio(PIN_IEC_ATN);
//     init_gpio(PIN_IEC_CLK_IN);
//     init_gpio(PIN_IEC_CLK_OUT);
//     init_gpio(PIN_IEC_DATA_IN);
//     init_gpio(PIN_IEC_DATA_OUT);
//     init_gpio(PIN_IEC_SRQ);
// #ifdef IEC_HAS_RESET
//     init_gpio(PIN_IEC_RESET);
// #endif

#ifdef IEC_INVERTED_LINES
#warning intr_type likely needs to be fixed!
#endif

    // Start task
    // Create a new high-priority task to handle the main service loop
    // This is assigned to CPU1; the WiFi task ends up on CPU0
    //xTaskCreatePinnedToCore(ml_iec_intr_task, "ml_iec_intr_task", MAIN_STACKSIZE, NULL, MAIN_PRIORITY, NULL, MAIN_CPUAFFINITY);

}


void systemBus::service()
{
  task();

  bool error = false, active = false;
  for(int i = 0; i < MAX_DISK_DEVICES; i++)
    {
      iecDrive *d = &(theFuji.get_disks(i)->disk_dev);
      error  |= d->hasError();
      active |= d->getNumOpenChannels()>0;
    }

  if( error )
    {
      static bool     flashState = false;
      static uint32_t prevFlash  = 0;
      if( (fnSystem.millis()-prevFlash) > 250 )
        {
          flashState = !flashState;
          fnLedManager.set(eLed::LED_BUS, flashState);
          prevFlash = fnSystem.millis();
        }
    }
  else
    fnLedManager.set(eLed::LED_BUS, active);
}


void systemBus::shutdown()
{
}


#endif /* BUILD_IEC */
