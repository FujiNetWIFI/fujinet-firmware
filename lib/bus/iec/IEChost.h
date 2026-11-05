#ifndef IECHOST_H
#define IECHOST_H

#include <stdint.h>
#include <string.h>

#include "IOChannel.h"

// Arduino-isms the vendored IECDevice library uses, for hosts. A host has no IEC lines:
// pins take any setting, every line reads back released (HIGH) and no interrupt ever
// fires, so the bus is driven only through NetIEC.
#define INPUT         0x0
#define OUTPUT        0x1
#define INPUT_PULLUP  0x2
#define LOW           0x0
#define HIGH          0x1
#define FALLING       0x2
#define RISING        0x1
#define bit(n) (1<<(n))

#define pinMode(pin, mode)       ((void)(pin), (void)(mode))
#define digitalWrite(pin, v)     ((void)(pin), (void)(v))
#define digitalRead(pin)         ((void)(pin), HIGH)
// IECBusHandler.cpp defines NOT_AN_INTERRUPT after its includes
#define digitalPinToInterrupt(p) NOT_AN_INTERRUPT

#define attachInterrupt(pin, fcn, mode) ((void)(pin), (void)(fcn), (void)(mode))
#define detachInterrupt(pin)            ((void)(pin))
#define noInterrupts() ((void)0)
#define interrupts()   ((void)0)

#define micros() ((uint32_t) GET_TIMESTAMP())

static inline void delayMicroseconds(uint32_t n)
{
  uint32_t s = micros();
  while((micros()-s)<n);
}

#define PSTR(x) x
#define strncmp_P strncmp
#define strcmp_P  strcmp
#define min(x, y) ((x)<(y) ? (x) : (y))
#define max(x, y) ((x)>(y) ? (x) : (y))

#endif
