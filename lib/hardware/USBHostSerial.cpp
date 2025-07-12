/*
Copyright (c) 2024 Bert Melis. All rights reserved.

This work is licensed under the terms of the MIT license.  
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.

Based on example code:
SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: CC0-1.0
*/

#include "USBHostSerial.h"

#if FUJINET_OVER_USB
#include <string.h>
#include "esp_err.h"

#include "utils.h"
#include "../../include/debug.h"

using namespace esp_usb;

USBHostSerial::USBHostSerial()
: _host_config{}
, _dev_config{}
, _line_coding{}
, _tx_buf_mem{}
, _rx_buf_mem{}
, _tx_buf_handle(nullptr)
, _rx_buf_handle(nullptr)
, _setupDone(false)
, _connected(false)
, _device_disconnected_sem(nullptr)
, _usb_lib_task_handle(nullptr) {
  _dev_config.connection_timeout_ms = 0;  // wait indefinitely for connection
  _dev_config.out_buffer_size = 512;
  _dev_config.in_buffer_size = 512;
  _dev_config.event_cb = _handle_event;
  _dev_config.data_cb = _handle_rx;
  _dev_config.user_arg = this;

  _tx_buf_handle = xRingbufferCreateStatic(USBHOSTSERIAL_BUFFERSIZE, RINGBUF_TYPE_BYTEBUF, _tx_buf_mem, &_tx_buf_data);
  _rx_buf_handle = xRingbufferCreateStatic(USBHOSTSERIAL_BUFFERSIZE, RINGBUF_TYPE_BYTEBUF, _rx_buf_mem, &_rx_buf_data);
  if (!_tx_buf_handle || !_rx_buf_handle) {
    abort();
  }
}

USBHostSerial::~USBHostSerial() {
  end();
}

USBHostSerial::operator bool() const {
  if (xSemaphoreTake(_device_disconnected_sem, 0) == pdTRUE) {
    xSemaphoreGive(_device_disconnected_sem);
    return false;
  }
  return true;
}

bool USBHostSerial::begin(int baud) {
  /*
  stopbits: 0: 1 stopbit, 1: 1.5 stopbits, 2: 2 stopbits
  parity: 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
  databits: 8
  */
  int stopbits = 0;
  int parity = 0;
  int databits = 8;

  if (!_setupDone) {
    _setupDone = true;
    _setup();
  }

  _line_coding.dwDTERate = baud;
  _line_coding.bCharFormat = stopbits;
  _line_coding.bParityType = parity;
  _line_coding.bDataBits = databits;

  if (xTaskCreate(_USBHostSerial_task, "usb_dev_lib", 4096, this, 1, NULL) == pdTRUE) {
    Debug_printf("USBHostSerial: Started @ %d baud\n", baud);
    return true;
  }
  return false;
}

void USBHostSerial::end() {
  if (_connected) {
    cdc_acm_host_uninstall();
    usb_host_uninstall();
    _connected = false;
  }

  if (_usb_lib_task_handle != nullptr) {
    vTaskDelete(_usb_lib_task_handle);
    _usb_lib_task_handle = nullptr;
  }

  if (_tx_buf_handle != nullptr) {
    vRingbufferDelete(_tx_buf_handle);
    _tx_buf_handle = nullptr;
  }
  if (_rx_buf_handle != nullptr) {
    vRingbufferDelete(_rx_buf_handle);
    _rx_buf_handle = nullptr;
  }
  Debug_printf("USBHostSerial: Serial stopped\n");
}

std::size_t USBHostSerial::write(uint8_t data) {
  if (xRingbufferSend(_tx_buf_handle, &data, 1, 10) == pdTRUE) {
    return 1;
  }
  return 0;
}

std::size_t USBHostSerial::write(const uint8_t *data, std::size_t len) {
  std::size_t i = 0;
  for (; i < len; ++i) {
    if (write(data[i]) == 1) {
      // Continue
    } else {
      break;
    }
  }
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial %i bytes written:\n", i);
  util_dump_bytes(data, len);
#endif
  return i;
}

size_t USBHostSerial::write(const char *str)
{
  std::size_t i = 0;
  int len = sizeof(str);

  for (; i < len; ++i) {
    if (write(str[i]) == 1) {
      ++i;
    } else {
      break;
    }
  }
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial: %i bytes written\n", i);
#endif
  return i;
}

size_t USBHostSerial::_print_number(unsigned long n, uint8_t base)
{
    char buf[8 * sizeof(long) + 1]; // Assumes 8-bit chars plus zero byte.
    char *str = &buf[sizeof(buf) - 1];

    *str = '\0';

    // prevent crash if called with base == 1
    if (base < 2)
        base = 10;

    do
    {
        unsigned long m = n;
        n /= base;
        char c = m - base * n;
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while (n);

    return write((const char *)str);
}

size_t USBHostSerial::print(const char *str)
{
    int z = strlen(str);

    return write((uint8_t *)str, z);
    ;
}

size_t USBHostSerial::print(const std::string &str)
{
    return print(str.c_str());
}

size_t USBHostSerial::print(int n, int base)
{
    return print((long)n, base);
}

size_t USBHostSerial::print(unsigned int n, int base)
{
    return print((unsigned long)n, base);
}

size_t USBHostSerial::print(long n, int base)
{
    if (base == 0)
    {
        return write(n);
    }
    else if (base == 10)
    {
        if (n < 0)
        {
            int t = print('-');
            n = -n;
            return _print_number(n, 10) + t;
        }
        return _print_number(n, 10);
    }
    else
    {
        return _print_number(n, base);
    }
}

size_t USBHostSerial::print(unsigned long n, int base)
{
    if (base == 0)
    {
        return write(n);
    }
    else
    {
        return _print_number(n, base);
    }
}

size_t USBHostSerial::println(const char *str)
{
    size_t n = print(str);
    n += println();
    return n;
}

size_t USBHostSerial::println(std::string str)
{
    size_t n = print(str);
    n += println();
    return n;
}

size_t USBHostSerial::println(int num, int base)
{
    size_t n = print(num, base);
    n += println();
    return n;
}



size_t USBHostSerial::printf(const char *fmt...)
{
    char *result = nullptr;
    va_list vargs;

    va_start(vargs, fmt);

    int z = vasprintf(&result, fmt, vargs);

    if (z > 0)
        write((uint8_t *)result, z);

    va_end(vargs);

    if (result != nullptr)
        free(result);

    return z >= 0 ? z : 0;
}

std::size_t USBHostSerial::available() {
  UBaseType_t numItemsWaiting;
  vRingbufferGetInfo(_rx_buf_handle, nullptr, nullptr, nullptr, nullptr, &numItemsWaiting);
  return numItemsWaiting;
}

uint8_t USBHostSerial::read() {
  std::size_t pxItemSize = 0;
  void* ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, 0, 1);
  if (ret) {
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial read: %x\n", *reinterpret_cast<uint8_t*>(ret));
#endif
    vRingbufferReturnItem(_rx_buf_handle, ret);
    return *reinterpret_cast<uint8_t*>(ret);
  }
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial read: 0 bytes\n");
#endif 
  return -1;
}

size_t USBHostSerial::readBytes(uint8_t *buffer, size_t length)
{
  std::size_t retVal = 0;
  std::size_t pxItemSize = 0;
  while (length > pxItemSize) {
    void *ret = xRingbufferReceiveUpTo(_rx_buf_handle, &pxItemSize, 10, length - pxItemSize);
    if (ret) {
      memcpy(buffer, ret, pxItemSize);
      retVal += pxItemSize;
      vRingbufferReturnItem(_rx_buf_handle, ret);
    } else {
      break;
    }
  }
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial readBytes %d:\n", retVal);
  util_dump_bytes(buffer, length);
#endif 
  return retVal;
}

void USBHostSerial::_setup() {
  _device_disconnected_sem = xSemaphoreCreateBinary();
  assert(_device_disconnected_sem);

  // Install USB Host driver. Should only be called once in entire application
  _host_config.skip_phy_setup = false;
  _host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  ESP_ERROR_CHECK(usb_host_install(&_host_config));

  // Create a task that will handle USB library events
  BaseType_t task_created = xTaskCreate(_usb_lib_task, "usb_lib", 4096, this, 1, &_usb_lib_task_handle);
  assert(task_created == pdTRUE);
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial: USB Lib Task Created\n");
#endif 
  ESP_ERROR_CHECK(cdc_acm_host_install(NULL));
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial: CDC Host Installed\n");
#endif 
  // Register VCP drivers to VCP service
  VCP::register_driver<FT23x>();
  VCP::register_driver<CP210x>();
  VCP::register_driver<CH34x>();
#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial: VCP Drivers Registered\n");
#endif 
}

bool USBHostSerial::_handle_rx(const uint8_t *data, size_t data_len, void *arg) {
  std::size_t lenReceived = 0;

#ifdef USB_HOST_DEBUG
  Debug_printf("USBHostSerial RECV: \n");
  util_dump_bytes(data, data_len);
#endif

  while (lenReceived < data_len) {
    if (xRingbufferSend(static_cast<USBHostSerial*>(arg)->_rx_buf_handle, &data[lenReceived], 1, 10) == pdTRUE) {
      ++lenReceived;
    } else {
      break;
    }
  }

  if (lenReceived < data_len) {
    // log overflow warning
#ifdef USB_HOST_DEBUG
    Debug_printf("USBHostSerial: USB RX buffer overflow!\n");
#endif
  }
  return true;
}

void USBHostSerial::_handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx) {
  switch (event->type) {
    case CDC_ACM_HOST_ERROR:
#ifdef USB_HOST_DEBUG
        Debug_printf("USBHostSerial: CDC-ACM error = %d\n", event->data.error);
#endif
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
#ifdef USB_HOST_DEBUG
        Debug_printf("USBHostSerial: CDC-ACM Device disconnected\n");
#endif
        xSemaphoreGive(static_cast<USBHostSerial*>(user_ctx)->_device_disconnected_sem);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
#ifdef USB_HOST_DEBUG
        Debug_printf("USBHostSerial: Serial state notif = 0x%04X\n", event->data.serial_state.val);
#endif
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
#ifdef USB_HOST_DEBUG
        Debug_printf("USBHostSerial: Network connection event = %s\n", event->data.network_connected ? "connected" : "disconnected");
#endif
    default: break;
  }
}

void USBHostSerial::_usb_lib_task(void *arg) {
  while (1) {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    Debug_printf("event_flags = 0x%08X\n", event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
#ifdef USB_HOST_DEBUG
      Debug_printf("USBHostSerial: All USB devices freed\n");
#endif
    }
  }
}

void USBHostSerial::set_baudrate(uint32_t baud)
{
    USBHostSerial* thisInstance = static_cast<USBHostSerial*>(NULL);
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&(thisInstance->_dev_config)));
    _line_coding.dwDTERate = baud;
    ESP_ERROR_CHECK(vcp->line_coding_set(&(thisInstance->_line_coding)));
}

void USBHostSerial::_USBHostSerial_task(void *arg)
{
  USBHostSerial* thisInstance = static_cast<USBHostSerial*>(arg);
  while (1) {
    auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&(thisInstance->_dev_config)));
    vTaskDelay( 10 / portTICK_PERIOD_MS );
    ESP_ERROR_CHECK(vcp->line_coding_set(&(thisInstance->_line_coding)));

    while (1) {
      // check for data to send
      std::size_t pxItemSize = 0;
      void *data = xRingbufferReceiveUpTo(thisInstance->_tx_buf_handle, &pxItemSize, 10, USBHOSTSERIAL_BUFFERSIZE);
      if (data) {
        ESP_ERROR_CHECK(vcp->tx_blocking((uint8_t*)data, pxItemSize));
        vRingbufferReturnItem(thisInstance->_tx_buf_handle, data);
      }
      if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        break;
      }
      taskYIELD();
    }
  }
}

#endif /* FUJINET_OVER_USB */