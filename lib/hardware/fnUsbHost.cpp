#include "fnUsbHost.h"

#if defined(CONFIG_USB_CDC_ACM_HOST_ENABLED) || defined(CONFIG_USB_PICOBOOT_HOST_ENABLED)

#include <usb/usb_host.h>
#include <esp_err.h>
#include <esp_system.h>

#include "../../include/debug.h"

static bool s_installed = false;

// Moved here verbatim from ACMChannel.cpp, which used to own it outright.
static void usb_lib_task(void *arg)
{
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            // Only fires once EVERY client has deregistered. PicoUpdater
            // stays registered for the life of the program on the boards
            // that have it, so there this is effectively unreachable.
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            Debug_printv("USB: All devices freed");
            // Continue handling USB events to allow device reconnection
        }
    }
}

bool usbHostEnsureInstalled(UBaseType_t event_task_priority)
{
    if (s_installed)
        return false;

    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096,
                                          xTaskGetCurrentTaskHandle(),
                                          event_task_priority, NULL);
    if (task_created != pdTRUE)
    {
        // Nothing on a USB-host board works without the event pump, and the
        // host library is already installed by now, so there is no partial
        // state worth returning to. abort() instead of assert(), which
        // compiles out under NDEBUG.
        Debug_printv("could not create usb_lib task, free internal/total heap: %lu/%lu",
                     esp_get_free_internal_heap_size(), esp_get_free_heap_size());
        abort();
    }

    s_installed = true;
    return true;
}

void usbHostRecycleRootPort()
{
    esp_err_t err = usb_host_lib_set_root_port_power(false);
    if (err != ESP_OK) {
        // ESP_ERR_INVALID_STATE here means the port was not powered in the
        // first place, so there is nothing to recycle and nothing to fix.
        Debug_printv("USB: root port power off failed (%s) -- not recycling",
                     esp_err_to_name(err));
        return;
    }

    // Long enough for the host stack to see the disconnect and tear the old
    // device down before the port comes back; CONFIG_USB_HOST_DEBOUNCE_DELAY_MS
    // (250) then governs how quickly the reattach is noticed.
    vTaskDelay(pdMS_TO_TICKS(100));

    err = usb_host_lib_set_root_port_power(true);
    if (err != ESP_OK) {
        Debug_printv("USB: root port power on FAILED (%s) -- devices on this "
                     "port will not reattach", esp_err_to_name(err));
        return;
    }

    Debug_printv("USB: root port recycled, awaiting re-enumeration");
}

#endif // CONFIG_USB_CDC_ACM_HOST_ENABLED || CONFIG_USB_PICOBOOT_HOST_ENABLED
