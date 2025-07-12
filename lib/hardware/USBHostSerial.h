/*
Copyright (c) 2024 Bert Melis. All rights reserved.

This work is licensed under the terms of the MIT license.  
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.

Based on example code:
SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: CC0-1.0
*/
#ifndef USBHOSTSERIAL_H
#define USBHOSTSERIAL_H

#include "sdkconfig.h" // for CONFIG_IDF_TARGET_ESP32S3

#if CONFIG_IDF_TARGET_ESP32S3
#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#include <usb/cdc_acm_host.h>
#include <usb/vcp_ch34x.hpp>
#include <usb/vcp_cp210x.hpp>
#include <usb/vcp_ftdi.hpp>
#include <usb/vcp.hpp>
#include <usb/usb_host.h>

#ifndef USBHOSTSERIAL_BUFFERSIZE
  #define USBHOSTSERIAL_BUFFERSIZE 256
#endif

class USBHostSerial {
 public:
  USBHostSerial();
  ~USBHostSerial();

  // true if serial-over-usb device is available eg. a device is connected
  explicit operator bool() const;

  bool begin(int baud);

  void end();

  // write one byte to serial-over-usb. returns 0 when buffer is full or device is not available
  std::size_t write(uint8_t data);

  // write data to serial-over-usb. returns length of data that was actually written: 0 when buffer is full or device is not available
  std::size_t write(const uint8_t *data, std::size_t len);
  
  size_t write(const char *s);

  size_t write(unsigned long n) { return write((uint8_t)n); };
  size_t write(long n) { return write((uint8_t)n); };
  size_t write(unsigned int n) { return write((uint8_t)n); };
  size_t write(int n) { return write((uint8_t)n); };

  size_t _print_number(unsigned long n, uint8_t base);

    size_t printf(const char *format, ...);

    //size_t println(const char *format, ...);
    size_t println(const char *str);
    size_t println() { return print("\r\n"); };
    size_t println(std::string str);
    size_t println(int num, int base = 10);

    //size_t print(const char *format, ...);
    size_t print(const char *str);
    size_t print(const std::string &str);
    size_t print(int n, int base = 10);
    size_t print(unsigned int n, int base = 10);
    size_t print(long n, int base = 10);
    size_t print(unsigned long n, int base = 10);

  // get size of available RX data
  std::size_t available();

  // read one byte from available data. If no data is available, 0 is returned (check with `available()`)
  uint8_t read();

  // read available data into `dest`. returns number of bytes written. maximum number of `size` bytes will be written
  size_t readBytes(uint8_t *buffer, size_t length);
  size_t readBytes(char *buffer, size_t length) { return readBytes((uint8_t *)buffer, length); };

  uint32_t get_baudrate(){ return _line_coding.dwDTERate; };
  void set_baudrate(uint32_t baud);

  // Stubs
  void flush(){};
  void flush_input(){};

 protected:
  usb_host_config_t _host_config;
  cdc_acm_host_device_config_t _dev_config;
  cdc_acm_line_coding_t _line_coding;
  uint8_t _tx_buf_mem[USBHOSTSERIAL_BUFFERSIZE];
  uint8_t _rx_buf_mem[USBHOSTSERIAL_BUFFERSIZE];
  RingbufHandle_t _tx_buf_handle;
  StaticRingbuffer_t _tx_buf_data;
  RingbufHandle_t _rx_buf_handle;
  StaticRingbuffer_t _rx_buf_data;
  bool _setupDone;
  bool _connected;

 private:
  void _setup();
  static bool _handle_rx(const uint8_t *data, size_t data_len, void *arg);
  static void _handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx);
  static void _usb_lib_task(void *arg);
  static void _usb_host_serial_task(void *arg);
  static void _USBHostSerial_task(void* arg);

  SemaphoreHandle_t _device_disconnected_sem;
  
  TaskHandle_t _usb_lib_task_handle;
  TaskHandle_t _usb_host_serial_task_handle;
};

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
#endif /* USBHOSTSERIAL_H */