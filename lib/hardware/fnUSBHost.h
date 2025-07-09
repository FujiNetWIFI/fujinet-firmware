#ifndef FNUSBHOST_H
#define FNUSBHOST_H

#include "sdkconfig.h" // for CONFIG_IDF_TARGET_ESP32S3

#if FUJINET_OVER_USB
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#define USB_HOST_RX_BUFFER_SIZE      512
#define VCP_BAUDRATE                 115200
#define VCP_STOP_BITS                0
#define VCP_PARITY                   0
#define VCP_DATA_BITS                8

class USBHost {
public:
    //USBHost();
    //~USBHost();

    /**
     * @brief Initialize the USB Host and CDC-ACM device.
     * 
     * @return true if initialization was successful, false otherwise.
     */
    bool init();

    /**
     * @brief Deinitialize the USB Host and release resources.
     */
    void deinit();

    /**
     * @brief Read data from the USB VCP.
     * 
     * @param data Pointer to the buffer where received data will be stored.
     * @param length Maximum number of bytes to read.
     * @return Number of bytes actually read.
     */
    size_t read(uint8_t* data, size_t length);

    /**
     * @brief Write data to the USB VCP.
     * 
     * @param data Pointer to the data buffer to be sent.
     * @param length Number of bytes to send.
     * @return Number of bytes actually written.
     */
    size_t write(const uint8_t* data, size_t length);

    /**
     * @brief Set the line coding parameters like baud rate, parity, etc.
     * 
     * @param baud_rate Desired baud rate.
     * @param data_bits Number of data bits.
     * @param parity Parity setting.
     * @param stop_bits Number of stop bits.
     */
    void set_line_coding(uint32_t baud_rate, uint8_t data_bits, uint8_t parity, uint8_t stop_bits);

    /**
     * @brief Check if the CDC-ACM device is connected.
     * 
     * @return true if connected, false otherwise.
     */
    bool is_connected();

private:
    //static void usb_event_callback(const usb_host_client_event_msg_t* event_msg, void* arg);
    //static void cdc_event_callback(const cdc_acm_host_dev_event_data_t* event_data, void* user_ctx);
    //static bool handle_rx(const uint8_t* data, size_t data_len, void* arg);

    usb_host_client_handle_t client_handle;
    cdc_acm_dev_hdl_t cdc_dev;
    bool device_connected;
    QueueHandle_t event_queue;
};
extern USBHost fnUSBHost;

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
#endif /* FNUSBHOST_H */