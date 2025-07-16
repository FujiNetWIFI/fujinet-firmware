#include "SerialACM.h"

#include <usb/usb_host.h>

#include "../../include/debug.h"

#define USB_HOST_PRIORITY   (20)
// FIXME - don't hard code, use whatever CDC-ADM device is connected
#define USB_DEVICE_VID (0xf022)
#define USB_DEVICE_PID (0x4001)

#define TX_STRING           ("CDC test string!")
#define TX_TIMEOUT_MS       (1000)

#include <inttypes.h> // debug
#include <esp_log.h>

#define DEBUG_TAG "SerialACM"

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
            Debug_printv("USB: All devices freed");
            // Continue handling USB events to allow device reconnection
        }
    }
}

static bool handle_rx(const uint8_t *data, size_t data_len, void *arg)
{
    SerialACM *serial = (SerialACM *) arg;

    
#if 1
    Debug_printv("ACM Data received: %.*s", data_len, data);
    ESP_LOG_BUFFER_HEXDUMP(DEBUG_TAG, data, data_len, ESP_LOG_INFO);
#endif
    //serial->write(data, data_len);
    return true;
}

static void handle_event_forwarder(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    SerialACM *instance = (SerialACM *) user_ctx;
    instance->handle_event(event);
    return;
}

void SerialACM::handle_event(const cdc_acm_host_dev_event_data_t *event)
{
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(DEBUG_TAG, "CDC-ACM error has occurred, err_no = %i", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        Debug_printv("Device suddenly disconnected");
        ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
        xSemaphoreGive(device_disconnected_sem);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        Debug_printv("Serial state notif 0x%04X", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
        ESP_LOGW(DEBUG_TAG, "Unsupported CDC event: %i", event->type);
        break;
    }
}

void SerialACM::begin(int baud)
{
    if (_initialized)
        abort();
    device_disconnected_sem = xSemaphoreCreateBinary();
    assert(device_disconnected_sem);

    // Install USB Host driver. Should only be called once in entire application
    Debug_printv("Installing USB Host");
    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create a task that will handle USB library events
    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096,
                                          xTaskGetCurrentTaskHandle(),
                                          USB_HOST_PRIORITY, NULL);
    assert(task_created == pdTRUE);

    Debug_printv("Installing CDC-ACM driver");
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    cdc_acm_host_device_config_t dev_config = {};
    dev_config.connection_timeout_ms = 1000;
    dev_config.out_buffer_size = 512;
    dev_config.in_buffer_size = 512;
    dev_config.user_arg = this;
    dev_config.event_cb = handle_event_forwarder;
    dev_config.data_cb = handle_rx;

    while (true) {
        // Open USB device from tusb_serial_device example
        // example. Either single or dual port configuration.
        Debug_printv("Opening CDC ACM device 0x%04X:0x%04X...", USB_DEVICE_VID,
                 USB_DEVICE_PID);
        esp_err_t err = cdc_acm_host_open(USB_DEVICE_VID, USB_DEVICE_PID,
                                          0, &dev_config, &cdc_dev);
        if (ESP_OK != err) {
#if 0
            Debug_printv("Opening CDC ACM device 0x%04X:0x%04X...",
                     USB_DEVICE_VID, USB_DEVICE_DUAL_PID);
            err = cdc_acm_host_open(USB_DEVICE_VID, USB_DEVICE_DUAL_PID,
                                    0, &dev_config, &cdc_dev);
#endif
            if (ESP_OK != err) {
                Debug_printv("Failed to open device");
                continue;
            }
        }
        //cdc_acm_host_desc_print(cdc_dev);
        vTaskDelay(pdMS_TO_TICKS(100));

#if 1
        // Test sending and receiving: responses are handled in handle_rx callback
        ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev,
                                                      (const uint8_t *) TX_STRING,
                                                      strlen(TX_STRING),
                                                      TX_TIMEOUT_MS));
#endif
        vTaskDelay(pdMS_TO_TICKS(100));

        // Test Line Coding commands: Get current line coding, change
        // it 9600 7N1 and read again
        Debug_printv("Setting up line coding");

        cdc_acm_line_coding_t line_coding;
        ESP_ERROR_CHECK(cdc_acm_host_line_coding_get(cdc_dev, &line_coding));
        Debug_printv("Line Get: Rate: %" PRIu32", Stop bits: %" PRIu8
                 ", Parity: %" PRIu8", Databits: %" PRIu8"",
                 line_coding.dwDTERate,
                 line_coding.bCharFormat, line_coding.bParityType, line_coding.bDataBits);

        line_coding.dwDTERate = 9600;
        line_coding.bDataBits = 7;
        line_coding.bParityType = 1;
        line_coding.bCharFormat = 1;
        ESP_ERROR_CHECK(cdc_acm_host_line_coding_set(cdc_dev, &line_coding));
        Debug_printv("Line Set: Rate: %" PRIu32", Stop bits: %" PRIu8
                 ", Parity: %" PRIu8", Databits: %" PRIu8"",
                 line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType,
                 line_coding.bDataBits);

        ESP_ERROR_CHECK(cdc_acm_host_line_coding_get(cdc_dev, &line_coding));
        Debug_printv("Line Get: Rate: %" PRIu32", Stop bits: %" PRIu8
                 ", Parity: %" PRIu8", Databits: %" PRIu8"",
                 line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType,
                 line_coding.bDataBits);

        ESP_ERROR_CHECK(cdc_acm_host_set_control_line_state(cdc_dev, true, false));

        // We are done. Wait for device disconnection and start over
        Debug_printv(
                 "Example finished successfully! You can reconnect the device to run again.");
        //xSemaphoreTake(device_disconnected_sem, portMAX_DELAY);
        break;
    }

    _initialized = true;
    return;
}

void SerialACM::end()
{
}

uint32_t SerialACM::get_baudrate()
{
    return 0;
}

void SerialACM::set_baudrate(uint32_t baud)
{
}

size_t SerialACM::available()
{
    return 0;
}

size_t SerialACM::read(void *buffer, size_t length)
{
    return 0;
}

size_t SerialACM::write(const void *buffer, size_t length)
{
    cdc_acm_host_data_tx_blocking(cdc_dev,
                                  (const uint8_t *) buffer,
                                  length,
                                  TX_TIMEOUT_MS);
    return length;
}

bool SerialACM::dtrState()
{
    return 0;
}

void SerialACM::flush()
{
    return;
}

void SerialACM::flush_input()
{
    return;
}

