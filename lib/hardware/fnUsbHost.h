#ifndef FNUSBHOST_H
#define FNUSBHOST_H

// One-time ownership of the ESP-IDF USB host library, shared by everything
// on this device that needs it.
//
// usb_host_install() may be called exactly once -- a second call asserts --
// but on a Fujiversal board two independent things want the host up:
// ACMChannel (the CDC-ACM link that carries FujiBus/DriveWire traffic to the
// companion MCU) and PicoUpdater (which flashes that companion over PICOBOOT
// before the bus ever starts). Whichever runs first installs; the other gets
// a no-op and a "someone beat me to it" answer.
//
// Boards with neither USB-host feature compile this file to nothing.

#if defined(CONFIG_USB_CDC_ACM_HOST_ENABLED) || defined(CONFIG_USB_PICOBOOT_HOST_ENABLED)

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Priority for the USB host library's event-pump task. Matches
// RS232_USB_BOOT_PRIORITY / DW_USB_BOOT_PRIORITY: USB servicing runs boosted
// through the cold-boot WiFi association storm and drops back once the
// station associates. Used by callers that install the host before a bus
// object exists to tell them its own preference.
#define FN_USB_HOST_BOOT_PRIORITY 24

// Installs the USB host library and its event-pump task if they are not up
// already. Returns true if THIS call did the install, false if it was
// already installed by someone else -- which matters, because a client
// registering after a device has already enumerated will not be told about
// it (see usbHostRecycleRootPort()).
//
// Call before registering a usb_host client, and as early as possible: a
// device attached at power-on can finish enumerating before a late client is
// listening.
//
// The event task is named "usb_lib"; ACMChannel::setServicePriority() finds
// it by that name, so do not rename it.
bool usbHostEnsureInstalled(UBaseType_t event_task_priority);

// Powers the root port off and back on, forcing every attached device to
// disconnect and re-enumerate.
//
// This exists because USB_HOST_CLIENT_EVENT_NEW_DEV is only ever sent at the
// moment a device enumerates (usb_host.c's USBH_EVENT_NEW_DEV handler), and
// is never replayed to a client that registers afterwards. So a client that
// comes up second -- the CDC-ACM driver on a board where PicoUpdater already
// brought the host up and enumerated the companion -- would otherwise wait
// forever for a device that is sitting right there, already enumerated.
// Recycling the port makes it announce itself again.
//
// Costs roughly half a second and is only correct while no one holds an open
// handle to a device on the port.
void usbHostRecycleRootPort();

#endif // CONFIG_USB_CDC_ACM_HOST_ENABLED || CONFIG_USB_PICOBOOT_HOST_ENABLED

#endif // FNUSBHOST_H
