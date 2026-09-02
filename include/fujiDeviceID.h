#ifndef FUJI_DEVICES_H
#define FUJI_DEVICES_H

#include <stdint.h>

typedef enum class FUJI_DEVICEID : uint8_t {
#if defined(BUILD_ADAM)
  FUJINET      = 0x0F,

  KEYBOARD     = 0x01,
  PRINTER      = 0x02,
  CLOCK        = 0x03,
  DISK         = 0x04,
  DISK2        = 0x05,
  DISK3        = 0x06,
  DISK4        = 0x07,
  TAPE         = 0x08,
  NETWORK      = 0x09,
  NETWORK_LAST = 0x0E,
#else
  FUJINET      = 0x70,

  DISK         = 0x31,
  DISK_LAST    = 0x3F,
  PRINTER      = 0x40,
  PRINTER_LAST = 0x43,
  VOICE        = 0x43,
  CLOCK        = 0x45,
  SIO2BT_SMART = 0x45, // Doubles as APETime and "High Score Submission" to URL
  ASPEQT       = 0x46,
  SIO2BT_NET   = 0x4E,
  TYPE3POLL    = 0x4F,
  SERIAL       = 0x50,
  SERIAL_LAST  = 0x53,
  CPM          = 0x5A,
  CASSETTE     = 0x5F,
  PCLINK       = 0x6F,
  NETWORK      = 0x71,
  NETWORK_LAST = 0x78,
  MIDI         = 0x99,
  DBC          = 0xFF,
#endif /* BUILD_ADAM */
} fujiDeviceID_t;

// Convenience methods to allow calculating disk & network IDs
inline constexpr fujiDeviceID_t operator+(fujiDeviceID_t lhs, uint8_t rhs) {
  return static_cast<fujiDeviceID_t>(static_cast<uint8_t>(lhs) + rhs);
}

inline constexpr uint8_t operator-(fujiDeviceID_t lhs, fujiDeviceID_t rhs) {
  return static_cast<uint8_t>(lhs) - static_cast<uint8_t>(rhs);
}

#endif /* FUJI_DEVICES_H */
