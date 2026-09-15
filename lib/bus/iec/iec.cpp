#ifdef BUILD_IEC

#include "iec.h"
#include "fujiDevice.h"
#include "fnSystem.h"
#include "led.h"
#include "debug.h"

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
      iecDrive *d = &(theFuji->get_disk(i)->disk_dev);
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

void systemBus::transaction_accept(transState_t expectMoreData)
{
  assert(_transaction_state == TRANS_STATE::INVALID);
  _transaction_state = expectMoreData;
}

void systemBus::transaction_success()
{
  assert(_transaction_state == TRANS_STATE::NO_GET
         || _transaction_state == TRANS_STATE::DID_GET);
  _transaction_state = TRANS_STATE::INVALID;
}

void systemBus::transaction_error()
{
  _transaction_state = TRANS_STATE::INVALID;
  // FIXME - signal error somehow
}

success_is_true systemBus::transaction_get(void *data, size_t len)
{
    assert(_transaction_state == TRANS_STATE::WILL_GET);
    assert(_activePacket);
    assert(data);
    _transaction_state = TRANS_STATE::DID_GET;
    len = std::min(len, _activePacket->data()->size());
    std::copy(_activePacket->data()->begin(), _activePacket->data()->begin() + len,
              static_cast<uint8_t *>(data));
    RETURN_SUCCESS_AS_TRUE();
}

void systemBus::transaction_send(const void *data, size_t len, bool err)
{
    assert(_transaction_state == TRANS_STATE::NO_GET);
    const uint8_t *ptr = reinterpret_cast<const uint8_t*>(data);
    _transaction_response.assign(ptr, ptr + len);
    Debug_printv("response:\r\n%s",
                 util_hexdump(_transaction_response.data(),
                              _transaction_response.size()).c_str());
    _transaction_state = TRANS_STATE::INVALID;
}

fujiDeviceID_t systemBus::fujiIDForDevice(iecDrive *device)
{
    for (uint8_t idx = 0; idx < MAX_DISK_DEVICES; idx++)
    {
        auto drv = &theFuji->get_disk(idx)->disk_dev;
        if (drv == device)
            return FUJI_DEVICEID::DISK + idx;
    }

    return (fujiDeviceID_t) 0;
}

#endif /* BUILD_IEC */
