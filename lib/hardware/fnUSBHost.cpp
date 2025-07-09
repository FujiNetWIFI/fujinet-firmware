#include "fnUSBHost.h"

#if FUJINET_OVER_USB
#include "utils.h"
#include "../../include/debug.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"
#include "usb/vcp.hpp"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

using namespace esp_usb;

USBHost fnUSBHost; // Global USB Host object
static SemaphoreHandle_t device_disconnected_sem;

// Buffer for received data
uint8_t rx_buffer[USB_HOST_RX_BUFFER_SIZE];
size_t rx_buffer_head;
size_t rx_buffer_tail;
SemaphoreHandle_t rx_buffer_mutex;  // To protect access to the buffer

/**
 * @brief USB Host library handling task
 *
 * @param arg Unused
 */
static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
#ifdef USB_HOST_DEBUG
            Debug_printv("fnUSBHost: All devices freed");
#endif            
            // Continue handling USB events to allow device reconnection
        }
    }
}

static void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        Debug_printf("USB CDC error, err_no = %i\n", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        Debug_printf("USB CDC suddenly disconnected!\n");
        ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
        xSemaphoreGive(device_disconnected_sem);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        Debug_printf("USB CDC Serial State: 0x%04X\n", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
        Debug_printf("USB CDC unsupported event %i\n", event->type);
        break;
    }
}

bool handle_rx(const uint8_t *data, size_t data_len, void *arg) {
#ifdef USB_HOST_DEBUG
    Debug_printf("USB CDC Data recv:\n");
    util_dump_bytes(data, data_len);
#endif

    if (rx_buffer_mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(rx_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        for (size_t i = 0; i < data_len; ++i) {
            size_t next_head = (rx_buffer_head + 1) % USB_HOST_RX_BUFFER_SIZE;
            if (next_head == rx_buffer_tail) {
                // Buffer overflow, drop data or handle overflow
#ifdef USB_HOST_DEBUG
                Debug_println("fnUSBHost: RX buffer overflow");
#endif
                break;
            }
            rx_buffer[rx_buffer_head] = data[i];
            rx_buffer_head = next_head;
        }
        xSemaphoreGive(rx_buffer_mutex);
        return true;
    } else {
        // Failed to take mutex
        return false;
    }
}

/**
 * @brief VCP library handling task
 *
 * @param arg Unused
 */
static void usb_vcp_task(void *arg)
{
    USBHost fnUSBHost;
    // Do everything else in a loop, so we can demonstrate USB device reconnections
    while (true) {
        const cdc_acm_host_device_config_t dev_config = {
            .connection_timeout_ms = 5000, // 5 seconds, enough time to plug the device in or experiment with timeout
            .out_buffer_size = 512,
            .in_buffer_size = 512,
            .event_cb = handle_event,
            .data_cb = handle_rx,
            .user_arg = NULL,
        };

        // You don't need to know the device's VID and PID. Just plug in any device and the VCP service will load correct (already registered) driver for the device
#ifdef USB_HOST_DEBUG
        Debug_printv("fnUSBHost: Opening any VCP device..");
#endif            
        auto vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&dev_config));

        if (vcp == nullptr) {
#ifdef USB_HOST_DEBUG
            Debug_printv("fnUSBHost: Failed to open any VCP");
#endif            
            continue;
        }
        vTaskDelay(10);

#ifdef USB_HOST_DEBUG
        Debug_printv("fnUSBHost: Setting up line encoding");
#endif            
        cdc_acm_line_coding_t line_coding = {
            .dwDTERate = VCP_BAUDRATE,
            .bCharFormat = VCP_STOP_BITS,
            .bParityType = VCP_PARITY,
            .bDataBits = VCP_DATA_BITS,
        };
        ESP_ERROR_CHECK(vcp->line_coding_set(&line_coding));

        /*
        Now the USB-to-UART converter is configured and receiving data.
        You can use standard CDC-ACM API to interact with it. E.g.

        ESP_ERROR_CHECK(vcp->set_control_line_state(false, true));
        ESP_ERROR_CHECK(vcp->tx_blocking((uint8_t *)"Test string", 12));
        */

        // Send some dummy data
        //ESP_LOGI(TAG, "Sending data through CdcAcmDevice");
        //uint8_t data[] = "test_string";
        //ESP_ERROR_CHECK(vcp->tx_blocking(data, sizeof(data)));
        //ESP_ERROR_CHECK(vcp->set_control_line_state(true, true));

        // We are done. Wait for device disconnection and start over
        //ESP_LOGI(TAG, "Done. You can reconnect the VCP device to run again.");
        //xSemaphoreTake(device_disconnected_sem, portMAX_DELAY);
    }
}

bool USBHost::init() {
    esp_err_t err;
    device_disconnected_sem = xSemaphoreCreateBinary();
    assert(device_disconnected_sem);

    // Install USB Host Library
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = 0,
    };
    err = usb_host_install(&host_config);
    if (err != ESP_OK) {
#ifdef USB_HOST_DEBUG
        Debug_printv("fnUSBHost: Failed to install USB Host Library: %s", esp_err_to_name(err));
#endif
        return false;
    }

    // Create a task that will handle USB library events
    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL);
    assert(task_created == pdTRUE);

    // Create event queue
    event_queue = xQueueCreate(10, sizeof(usb_host_client_event_msg_t));
    if (event_queue == nullptr) {
#ifdef USB_HOST_DEBUG
        Debug_println("fnUSBHost: Failed to create event queue");
#endif
        usb_host_uninstall();
        return false;
    }

    // Register VCP drivers to VCP service
    VCP::register_driver<FT23x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<CH34x>();

//#ifdef USB_HOST_DEBUG
    Debug_println("fnUSBHost: USB Host and VCP Drivers initialized successfully");
//#endif
    return true;
}

void USBHost::deinit() {
    if (cdc_dev != nullptr) {
        cdc_acm_host_close(cdc_dev);
        cdc_dev = nullptr;
    }
    cdc_acm_host_uninstall();
    if (client_handle != nullptr) {
        usb_host_client_deregister(client_handle);
        client_handle = nullptr;
    }
    if (event_queue != nullptr) {
        vQueueDelete(event_queue);
        event_queue = nullptr;
    }
    usb_host_uninstall();
#ifdef USB_HOST_DEBUG
    Debug_println("fnUSBHost: USB Host and CDC-ACM deinitialized");
#endif
}

size_t USBHost::read(uint8_t* data, size_t length) {
    if (rx_buffer_mutex == nullptr) {
        return 0;
    }
    if (xSemaphoreTake(rx_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        size_t available_data = (rx_buffer_head >= rx_buffer_tail) ? (rx_buffer_head - rx_buffer_tail) :
                                (USB_HOST_RX_BUFFER_SIZE - rx_buffer_tail + rx_buffer_head);

        size_t bytes_to_read = (length < available_data) ? length : available_data;

        for (size_t i = 0; i < bytes_to_read; ++i) {
            data[i] = rx_buffer[rx_buffer_tail];
            rx_buffer_tail = (rx_buffer_tail + 1) % USB_HOST_RX_BUFFER_SIZE;
        }

        xSemaphoreGive(rx_buffer_mutex);
        return bytes_to_read;
    } else {
        // Failed to take mutex
        return 0;
    }
}

size_t USBHost::write(const uint8_t* data, size_t length) {
    if (!device_connected || cdc_dev == nullptr) {
#ifdef USB_HOST_DEBUG
        Debug_println("fnUSBHost: Device not connected");
#endif
        return 0;
    }

    esp_err_t err = cdc_acm_host_data_tx_blocking(cdc_dev, data, length, 1000);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
#ifdef USB_HOST_DEBUG
        Debug_printv("fnUSBHost: Error writing data: %s", esp_err_to_name(err));
#endif
    }
    return length;
}

void USBHost::set_line_coding(uint32_t baud_rate, uint8_t data_bits, uint8_t parity, uint8_t stop_bits) {
    if (!device_connected || cdc_dev == nullptr) {
#ifdef USB_HOST_DEBUG
        Debug_println("fnUSBHost: Device not connected");
#endif
        return;
    }

    cdc_acm_line_coding_t line_coding = {
        .dwDTERate = baud_rate,
        .bCharFormat = stop_bits,
        .bParityType = parity,
        .bDataBits = data_bits,
    };

    esp_err_t err = cdc_acm_host_line_coding_set(cdc_dev, &line_coding);
    if (err != ESP_OK) {
#ifdef USB_HOST_DEBUG
        Debug_printv("fnUSBHost: Failed to set line coding: %s", esp_err_to_name(err));
#endif
    } else {
#ifdef USB_HOST_DEBUG
        Debug_println("fnUSBHost: Line coding set successfully");
#endif
    }
}

bool USBHost::is_connected() {
    return device_connected;
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 */