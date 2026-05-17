#include "ACMChannel.h"

#ifdef CONFIG_USB_CDC_ACM_HOST_ENABLED

#include <usb/usb_host.h>

#include "../../include/debug.h"

#define USB_HOST_PRIORITY   (20)

#define TX_TIMEOUT_MS       (1000)

#include <inttypes.h> // debug
#include <esp_log.h>

#define DEBUG_TAG "ACMChannel"

#define MAX_FIFO_PAYLOAD 32
typedef struct {
    size_t length;
    uint8_t data[MAX_FIFO_PAYLOAD];
} FIFOPacket;

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

static bool rxForwarder(const uint8_t *data, size_t length, void *arg)
{
    ACMChannel *instance = (ACMChannel *) arg;

    instance->dataReceived(data, length);
    return true;
}

void ACMChannel::dataReceived(const uint8_t *data, size_t length)
{
    size_t offset;
    FIFOPacket pkt;
    BaseType_t woken;


    //Debug_printv("received %i", length);
    for (offset = 0; length; offset += pkt.length, length -= pkt.length)
    {
        pkt.length = std::min(length, (size_t) MAX_FIFO_PAYLOAD);
        memcpy(pkt.data, data + offset, pkt.length);
        xQueueSendFromISR(rxQueue, &pkt, &woken);
    }

    return;
}

static void eventForwarder(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    ACMChannel *instance = (ACMChannel *) user_ctx;
    instance->eventReceived(event);
    return;
}

void ACMChannel::eventReceived(const cdc_acm_host_dev_event_data_t *event)
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
        _serial_state = event->data.serial_state;
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
        ESP_LOGW(DEBUG_TAG, "Unsupported CDC event: %i", event->type);
        break;
    }
}

void ACMChannel::newDevice(usb_device_handle_t usb_dev)
{
    Debug_printv("newDevCallback fired");

    const usb_device_desc_t *dev_desc;
    usb_host_get_device_descriptor(usb_dev, &dev_desc);
    Debug_printv("VID: 0x%04X PID: 0x%04X", dev_desc->idVendor, dev_desc->idProduct);

    const usb_config_desc_t *config_desc;
    usb_host_get_active_config_descriptor(usb_dev, &config_desc);

    int offset = 0;
    const usb_standard_desc_t *desc = (const usb_standard_desc_t *)config_desc;
    uint16_t total_len = config_desc->wTotalLength;

    while ((desc = usb_parse_next_descriptor_of_type(
                desc, total_len, USB_B_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION, &offset)) != NULL)
    {
        const usb_iad_desc_t *iad = (const usb_iad_desc_t *)desc;
        Debug_printv("IAD: class=%d subclass=%d firstIface=%d",
            iad->bFunctionClass, iad->bFunctionSubClass, iad->bFirstInterface);

        if (iad->bFunctionClass == USB_CLASS_COMM &&
            iad->bFunctionSubClass == USB_CDC_SUBCLASS_ACM)
        {
            Debug_printv("Found CDC-ACM IAD");
            found_vid = dev_desc->idVendor;
            found_pid = dev_desc->idProduct;
            found_interface = iad->bFirstInterface;
            xSemaphoreGive(device_connected_sem);
            return;
        }
    }
    Debug_printv("No CDC-ACM IAD found");
}

// FIXME - apparently it later ESP-DIF versions there's a `void *user_arg`
static ACMChannel *ndc_instance = nullptr;
static void newDevForwarder(usb_device_handle_t usb_dev)
{
    ndc_instance->newDevice(usb_dev);
    return;
}

void ACMChannel::begin()
{
    rxQueue = xQueueCreate(1024 / MAX_FIFO_PAYLOAD, sizeof(FIFOPacket));
    device_disconnected_sem = xSemaphoreCreateBinary();
    device_connected_sem = xSemaphoreCreateBinary();  // <-- new
    assert(device_disconnected_sem);
    assert(device_connected_sem);

    Debug_printv("Installing USB Host");
    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096,
                                          xTaskGetCurrentTaskHandle(),
                                          USB_HOST_PRIORITY, NULL);
    assert(task_created == pdTRUE);

    Debug_printv("Installing CDC-ACM driver");

    ndc_instance = this;

    // Register the new-device callback before installing, so we don't miss
    // devices that were already connected at boot
    cdc_acm_host_driver_config_t driver_config = {};
    driver_config.driver_task_stack_size = 4096;
    driver_config.driver_task_priority = USB_HOST_PRIORITY;
    driver_config.xCoreID = 0;
    driver_config.new_dev_cb = newDevForwarder;
    ESP_ERROR_CHECK(cdc_acm_host_install(&driver_config));

    cdc_acm_host_device_config_t dev_config = {};
    dev_config.connection_timeout_ms = 1000;
    dev_config.out_buffer_size = 512;
    dev_config.in_buffer_size = 512;
    dev_config.user_arg = this;
    dev_config.event_cb = eventForwarder;
    dev_config.data_cb = rxForwarder;

    while (true) {
        // Wait for newDevCallback to find a CDC-ACM device
        Debug_printv("Waiting for CDC-ACM device...");
        xSemaphoreTake(device_connected_sem, portMAX_DELAY);

        Debug_printv("Opening CDC ACM device 0x%04X:0x%04X...", found_vid, found_pid);
        esp_err_t err = cdc_acm_host_open_vendor_specific(found_vid, found_pid,
                                                          found_interface,
                                                          &dev_config, &cdc_dev);

        if (err != ESP_OK) {
            Debug_printv("Failed to open device, waiting for next...");
            continue;
        }
        //cdc_acm_host_desc_print(cdc_dev);
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

    return;
}

void ACMChannel::end()
{
}

void ACMChannel::updateFIFO()
{
    FIFOPacket pkt;
    size_t old_len;

    while (xQueueReceive(rxQueue, &pkt, 0))
    {
        //Debug_printv("packet %i", pkt.length);
        old_len = _fifo.size();
        _fifo.resize(old_len + pkt.length);
        memcpy(&_fifo[old_len], pkt.data, pkt.length);
        //Debug_printv("fifo %i", _fifo.size());
    }

    return;
}

size_t ACMChannel::dataOut(const void *buffer, size_t length)
{
    cdc_acm_host_data_tx_blocking(cdc_dev,
                                  (const uint8_t *) buffer,
                                  length,
                                  TX_TIMEOUT_MS);
    return length;
}

void ACMChannel::flushOutput()
{
    return;
}

bool ACMChannel::getDTR()
{
    return _serial_state.bTxCarrier;
}

void ACMChannel::setDSR(bool state)
{
    _dsr = state;
    cdc_acm_host_set_control_line_state(cdc_dev, _dsr, _cts);
}

bool ACMChannel::getRTS()
{
    return 1;
}

void ACMChannel::setCTS(bool state)
{
    _cts = state;
    cdc_acm_host_set_control_line_state(cdc_dev, _dsr, _cts);
}

bool ACMChannel::getDCD()
{
    return _serial_state.bRxCarrier;
}

bool ACMChannel::getRI()
{
    return _serial_state.bRingSignal;
}

#endif /* CONFIG_USB_CDC_ACM_HOST_ENABLED */
