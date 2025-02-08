// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
//
// This implementation is based on the code used in the VICE emulator.
// The corresponding code in VICE (src/serial/serial-iec-device.c) was 
// contributed to VICE by me in 2003.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#include "IECBusHandler.h"
#include "IECDevice.h"

#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "IECespidf.h"
#endif

#ifndef ESP_IDF_VERSION_VAL
#define ESP_IDF_VERSION_VAL(x,y,z) 0
#endif

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT -1
#endif

#ifndef INTERRUPT_FCN_ARG
#define INTERRUPT_FCN_ARG
#endif

#if MAX_DEVICES>30
#error "Maximum allowed number of devices is 30"
#endif

//#define JDEBUG

// ---------------- Arduino 8-bit ATMega (UNO R3/Mega/Mini/Micro/Leonardo...)

#if defined(__AVR__)

#if defined(__AVR_ATmega32U4__)
// Atmega32U4 does not have a second 8-bit timer (first one is used by Arduino millis())
// => use lower 8 bit of 16-bit timer 1
#define timer_init()         { TCCR1A=0; TCCR1B=0; }
#define timer_reset()        TCNT1L=0
#define timer_start()        TCCR1B |= bit(CS11)
#define timer_stop()         TCCR1B &= ~bit(CS11)
#define timer_less_than(us)  (TCNT1L < ((uint8_t) (2*(us))))
#define timer_wait_until(us) while( timer_less_than(us) )
#else
// use 8-bit timer 2 with /8 prescaler
#define timer_init()         { TCCR2A=0; TCCR2B=0; }
#define timer_reset()        TCNT2=0
#define timer_start()        TCCR2B |= bit(CS21)
#define timer_stop()         TCCR2B &= ~bit(CS21)
#define timer_less_than(us)  (TCNT2 < ((uint8_t) (2*(us))))
#define timer_wait_until(us) while( timer_less_than(us) )
#endif

//NOTE: Must disable SUPPORT_DOLPHIN, otherwise no pins left for debugging (except Mega)
#ifdef JDEBUG
#define JDEBUGI() DDRD |= 0x80; PORTD &= ~0x80 // PD7 = pin digital 7
#define JDEBUG0() PORTD&=~0x80
#define JDEBUG1() PORTD|=0x80
#endif

// ---------------- Arduino Uno R4

#elif defined(ARDUINO_UNOR4_MINIMA) || defined(ARDUINO_UNOR4_WIFI)
#ifndef ARDUINO_UNOR4
#define ARDUINO_UNOR4
#endif

// NOTE: this assumes the AGT timer is running at the (Arduino default) 3MHz rate
//       and rolling over after 3000 ticks 
static unsigned long timer_start_ticks;
static uint16_t timer_ticks_diff(uint16_t t0, uint16_t t1) { return ((t0 < t1) ? 3000 + t0 : t0) - t1; }
#define timer_init()         while(0)
#define timer_reset()        timer_start_ticks = R_AGT0->AGT
#define timer_start()        timer_start_ticks = R_AGT0->AGT
#define timer_stop()         while(0)
#define timer_less_than(us)  (timer_ticks_diff(timer_start_ticks, R_AGT0->AGT) < ((int) (us*3)))
#define timer_wait_until(us) while( timer_less_than(us) )

#ifdef JDEBUG
#define JDEBUGI() pinMode(1, OUTPUT)
#define JDEBUG0() R_PORT3->PODR &= ~bit(2);
#define JDEBUG1() R_PORT3->PODR |=  bit(2);
#endif

// ---------------- Arduino Due

#elif defined(__SAM3X8E__)

#define portModeRegister(port) 0

static unsigned long timer_start_ticks;
static uint32_t timer_ticks_diff(uint32_t t0, uint32_t t1) { return ((t0 < t1) ? 84000 + t0 : t0) - t1; }
#define timer_init()         while(0)
#define timer_reset()        timer_start_ticks = SysTick->VAL;
#define timer_start()        timer_start_ticks = SysTick->VAL;
#define timer_stop()         while(0)
#define timer_less_than(us)  (timer_ticks_diff(timer_start_ticks, SysTick->VAL) < ((int) (us*84)))
#define timer_wait_until(us) while( timer_less_than(us) )

#ifdef JDEBUG
#define JDEBUGI() pinMode(2, OUTPUT)
#define JDEBUG0() digitalWriteFast(2, LOW);
#define JDEBUG1() digitalWriteFast(2, HIGH);
#endif

// ---------------- Raspberry Pi Pico

#elif defined(ARDUINO_ARCH_RP2040)

// note: micros() call on MBED core is SLOW - using time_us_32() instead
static unsigned long timer_start_us;
#define timer_init()         while(0)
#define timer_reset()        timer_start_us = time_us_32()
#define timer_start()        timer_start_us = time_us_32()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((time_us_32()-timer_start_us) < ((int) (us+0.5)))
#define timer_wait_until(us) while( timer_less_than(us) )

#ifdef JDEBUG
#define JDEBUGI() pinMode(20, OUTPUT)
#define JDEBUG0() gpio_put(20, 0)
#define JDEBUG1() gpio_put(20, 1)
#endif

// ---------------- ESP32

#elif defined(ESP_PLATFORM)

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_clk.h"
#define esp_cpu_cycle_count_t uint32_t
#define esp_cpu_get_cycle_count esp_cpu_get_ccount
#define esp_rom_get_cpu_ticks_per_us() (esp_clk_cpu_freq()/1000000)
#endif
static DRAM_ATTR esp_cpu_cycle_count_t timer_start_cycles, timer_cycles_per_us;
#define timer_init()         timer_cycles_per_us = esp_rom_get_cpu_ticks_per_us()
#define timer_reset()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_start()        timer_start_cycles = esp_cpu_get_cycle_count()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((esp_cpu_get_cycle_count()-timer_start_cycles) < (uint32_t(us+0.5)*timer_cycles_per_us))
#define timer_wait_until(us) timer_wait_until_(us+0.5)
FORCE_INLINE_ATTR void timer_wait_until_(uint32_t us)
{
  // using esp_cpu_get_cycle_count() instead of esp_timer_get_time() works much
  // better in timing-critical code since it translates into a single CPU instruction
  // instead of a library call that may introduce short delays due to flash ROM access
  // conflicts with the other core
  esp_cpu_cycle_count_t to = us * timer_cycles_per_us;
  while( (esp_cpu_get_cycle_count()-timer_start_cycles) < to );
}

// interval in which we need to feed the interrupt WDT to stop it from re-booting the system
#define IWDT_FEED_TIME ((CONFIG_ESP_INT_WDT_TIMEOUT_MS-10)*1000)

// keep track whether interrupts are enabled or not (see comments in waitPinDATA/waitPinCLK)
static bool haveInterrupts = true;
#undef noInterrupts
#undef interrupts
#define noInterrupts() { portDISABLE_INTERRUPTS(); haveInterrupts = false; }
#define interrupts()   { haveInterrupts = true; portENABLE_INTERRUPTS(); }

#if defined(JDEBUG)
#define JDEBUGI() pinMode(12, OUTPUT)
#define JDEBUG0() GPIO.out_w1tc = bit(12)
#define JDEBUG1() GPIO.out_w1ts = bit(12)
#endif

// ---------------- other (32-bit) platforms

#else

static unsigned long timer_start_us;
#define timer_init()         while(0)
#define timer_reset()        timer_start_us = micros()
#define timer_start()        timer_start_us = micros()
#define timer_stop()         while(0)
#define timer_less_than(us)  ((micros()-timer_start_us) < ((int) (us+0.5)))
#define timer_wait_until(us) while( timer_less_than(us) )

#if defined(JDEBUG) && defined(ESP_PLATFORM)
#define JDEBUGI() pinMode(26, OUTPUT)
#define JDEBUG0() GPIO.out_w1tc = bit(26)
#define JDEBUG1() GPIO.out_w1ts = bit(26)
#endif

#endif

#ifndef JDEBUG
#define JDEBUGI()
#define JDEBUG0()
#define JDEBUG1()
#endif

#if defined(__SAM3X8E__)
// Arduino Due
#define pinModeFastExt(pin, reg, bit, dir)    { if( (dir)==OUTPUT ) digitalPinToPort(pin)->PIO_OER |= bit; else digitalPinToPort(pin)->PIO_ODR |= bit; }
#define digitalReadFastExt(pin, reg, bit)     (*(reg) & (bit))
#define digitalWriteFastExt(pin, reg, bit, v) { if( v ) *(reg)|=(bit); else (*reg)&=~(bit); }
#elif defined(ARDUINO_ARCH_RP2040)
// Raspberry Pi Pico
#define pinModeFastExt(pin, reg, bit, dir)    gpio_set_dir(pin, (dir)==OUTPUT)
#define digitalReadFastExt(pin, reg, bit)     gpio_get(pin)
#define digitalWriteFastExt(pin, reg, bit, v) gpio_put(pin, v)
#elif defined(__AVR__) || defined(ARDUINO_UNOR4)
// Arduino 8-bit (Uno R3/Mega/...)
#define pinModeFastExt(pin, reg, bit, dir)    { if( (dir)==OUTPUT ) *(reg)|=(bit); else *(reg)&=~(bit); }
#define digitalReadFastExt(pin, reg, bit)     (*(reg) & (bit))
#define digitalWriteFastExt(pin, reg, bit, v) { if( v ) *(reg)|=(bit); else (*reg)&=~(bit); }
#elif defined(ESP_PLATFORM)
// ESP32
#define pinModeFastExt(pin, reg, bit, dir)    { if( (dir)==OUTPUT ) *(reg)|=(bit); else *(reg)&=~(bit); }
#define digitalReadFastExt(pin, reg, bit)     (*(reg) & (bit))
#define digitalWriteFastExt(pin, reg, bit, v) { if( v ) *(reg)|=(bit); else (*reg)&=~(bit); }
#else
#warning "No fast digital I/O macros defined for this platform - code will likely run too slow"
#define pinModeFastExt(pin, reg, bit, dir)    pinMode(pin, dir)
#define digitalReadFastExt(pin, reg, bit)     digitalRead(pin)
#define digitalWriteFastExt(pin, reg, bit, v) digitalWrite(pin, v)
#endif

// on ESP32 we need very timing-critical code (i.e. transmitting/receiving data
// under fast-load protocols) to reside in IRAM in order to avoid short delays
// due to flash ROM access conflicts with the other core
#ifndef IRAM_ATTR
#ifdef ESP_PLATFORM
#error "Expected IRAM_ATTR to be defined"
#else
#define IRAM_ATTR
#endif
#endif


// delayMicroseconds on some platforms does not work if called when interrupts are disabled
// => define a version that does work on all supported platforms
static IRAM_ATTR void delayMicrosecondsISafe(uint16_t t)
{
  timer_init();
  timer_start();
  while( t>125 ) { timer_wait_until(125); timer_reset(); t -= 125; }
  timer_wait_until(t);
  timer_stop();
}


// -----------------------------------------------------------------------------------------

#define P_ATN        0x80
#define P_LISTENING  0x40
#define P_TALKING    0x20
#define P_DONE       0x10
#define P_RESET      0x08

#define S_JIFFY_ENABLED          0x0001  // JiffyDos support is enabled
#define S_JIFFY_DETECTED         0x0002  // Detected JiffyDos request from host
#define S_JIFFY_BLOCK            0x0004  // Detected JiffyDos block transfer request from host
#define S_DOLPHIN_ENABLED        0x0008  // DolphinDos support is enabled
#define S_DOLPHIN_DETECTED       0x0010  // Detected DolphinDos request from host
#define S_DOLPHIN_BURST_ENABLED  0x0020  // DolphinDos burst mode is enabled
#define S_DOLPHIN_BURST_TRANSMIT 0x0040  // Detected DolphinDos burst transmit request from host
#define S_DOLPHIN_BURST_RECEIVE  0x0080  // Detected DolphinDos burst receive request from host
#define S_EPYX_ENABLED           0x0100  // Epyx FastLoad support is enabled
#define S_EPYX_HEADER            0x0200  // Read EPYX FastLoad header (drive code transmission)
#define S_EPYX_LOAD              0x0400  // Detected Epyx "load" request
#define S_EPYX_SECTOROP          0x0800  // Detected Epyx "sector operation" request

#define TC_NONE      0
#define TC_DATA_LOW  1
#define TC_DATA_HIGH 2
#define TC_CLK_LOW   3
#define TC_CLK_HIGH  4


IECBusHandler *IECBusHandler::s_bushandler = NULL;


void IRAM_ATTR IECBusHandler::writePinCLK(bool v)
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pun to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinCLK, m_regCLKmode, m_bitCLK, v ? INPUT : OUTPUT);
}


void IRAM_ATTR IECBusHandler::writePinDATA(bool v)
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pun to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinDATA, m_regDATAmode, m_bitDATA, v ? INPUT : OUTPUT);
}


void IRAM_ATTR IECBusHandler::writePinCTRL(bool v)
{
  if( m_pinCTRL!=0xFF )
    digitalWrite(m_pinCTRL, v);
}


bool IRAM_ATTR IECBusHandler::readPinATN()
{
  return digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN)!=0;
}


bool IRAM_ATTR IECBusHandler::readPinCLK()
{
  return digitalReadFastExt(m_pinCLK, m_regCLKread, m_bitCLK)!=0;
}


bool IRAM_ATTR IECBusHandler::readPinDATA()
{
  return digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA)!=0;
}


bool IRAM_ATTR IECBusHandler::readPinRESET()
{
  if( m_pinRESET==0xFF ) return true;
  return digitalReadFastExt(m_pinRESET, m_regRESETread, m_bitRESET)!=0;
}


bool IECBusHandler::waitTimeout(uint16_t timeout, uint8_t cond)
{
  // This function may be called in code where interrupts are disabled.
  // Calling micros() when interrupts are disabled does not work on all
  // platforms, some return incorrect values, others may re-enable interrupts
  // So we use our high-precision timer. However, on some platforms that timer
  // can only count up to 127 microseconds so we have to go in small increments.

  timer_init();
  timer_reset();
  timer_start();
  while( true )
    {
      switch( cond )
        {
        case TC_DATA_LOW:
          if( readPinDATA() == LOW  ) return true;
          break;

        case TC_DATA_HIGH:
          if( readPinDATA() == HIGH ) return true;
          break;

        case TC_CLK_LOW:
          if( readPinCLK()  == LOW  ) return true;
          break;

        case TC_CLK_HIGH:
          if( readPinCLK()  == HIGH ) return true;
          break;
        }

      if( ((m_flags & P_ATN)!=0) == readPinATN() )
        {
          // ATN changed state => abort with FALSE
          return false;
        }
      else if( timeout<100 )
        {
          if( !timer_less_than(timeout) )
            {
              // timeout has expired => if there was no condition to wait for
              // then return TRUE, otherwise return FALSE (because the condition was not met)
              return cond==TC_NONE;
            }
        }
      else if( !timer_less_than(100) )
        {
          // subtracting from the timout value like below is not 100% precise (we may wait
          // a few microseconds too long because the timer may already have counter further)
          // but this function is not meant for SUPER timing-critical code so that's ok.
          timer_reset();
          timeout -= 100;
        }
    }
}


void IECBusHandler::waitPinATN(bool state)
{
#ifdef ESP_PLATFORM
  // waiting indefinitely with interrupts disabled on ESP32 is bad because
  // the interrupt WDT will reboot the system if we wait too long
  // => if interrupts are disabled then briefly enable them before the timeout to "feed" the WDT
  uint64_t t = esp_timer_get_time();
  while( readPinATN()!=state )
    {
      if( !haveInterrupts && (esp_timer_get_time()-t)>IWDT_FEED_TIME )
        {
          interrupts(); noInterrupts();
          t = esp_timer_get_time();
        }
    }
#else
  while( readPinATN()!=state );
#endif
}


bool IECBusHandler::waitPinDATA(bool state, uint16_t timeout)
{
  // (if timeout is not given it defaults to 1000us)
  // if ATN changes (i.e. our internal ATN state no longer matches the ATN signal line)
  // or the timeout is met then exit with error condition
  if( timeout==0 )
    {
#ifdef ESP_PLATFORM
      // waiting indefinitely with interrupts disabled on ESP32 is bad because
      // the interrupt WDT will reboot the system if we wait too long
      // => if interrupts are disabled then briefly enable them before the timeout to "feed" the WDT
      uint64_t t = esp_timer_get_time();
      while( readPinDATA()!=state )
        {
          if( ((m_flags & P_ATN)!=0) == readPinATN() )
            return false;
          else if( !haveInterrupts && (esp_timer_get_time()-t)>IWDT_FEED_TIME )
            {
              interrupts(); noInterrupts();
              t = esp_timer_get_time();
            }
        }
#else
      // timeout is 0 (no timeout)
      while( readPinDATA()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_DATA_HIGH : TC_DATA_LOW) ) return false;
    }

  // DATA LOW can only be properly detected if ATN went HIGH->LOW
  // (m_flags&ATN)==0 and readPinATN()==0)
  // since other devices may have pulled DATA LOW
  return state || (m_flags & P_ATN) || readPinATN();
}


bool IECBusHandler::waitPinCLK(bool state, uint16_t timeout)
{
  // (if timeout is not given it defaults to 1000us)
  // if ATN changes (i.e. our internal ATN state no longer matches the ATN signal line)
  // or the timeout is met then exit with error condition
  if( timeout==0 )
    {
#ifdef ESP_PLATFORM
      // waiting indefinitely with interrupts disabled on ESP32 is bad because
      // the interrupt WDT will reboot the system if we wait too long
      // => if interrupts are disabled then briefly enable them before the timeout to "feed" the WDT
      uint64_t t = esp_timer_get_time();
      while( readPinCLK()!=state )
        {
          if( ((m_flags & P_ATN)!=0) == readPinATN() )
            return false;
          else if( !haveInterrupts && (esp_timer_get_time()-t)>IWDT_FEED_TIME )
            {
              interrupts(); noInterrupts();
              t = esp_timer_get_time();
            }
        }
#else
      // timeout is 0 (no timeout)
      while( readPinCLK()!=state )
        if( ((m_flags & P_ATN)!=0) == readPinATN() )
          return false;
#endif
    }
  else
    {
      // if waitTimeout for the given condition fails then exit
      if( !waitTimeout(timeout, state ? TC_CLK_HIGH : TC_CLK_LOW) ) return false;
    }
  
  return true;
}


void IECBusHandler::sendSRQ()
{
  if( m_pinSRQ!=0xFF )
    {
      digitalWrite(m_pinSRQ, LOW);
      pinMode(m_pinSRQ, OUTPUT);
      delayMicrosecondsISafe(1);
      pinMode(m_pinSRQ, INPUT);
    }
}


IECBusHandler::IECBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinDATA, uint8_t pinRESET, uint8_t pinCTRL, uint8_t pinSRQ)
#if defined(SUPPORT_DOLPHIN)
#if defined(SUPPORT_DOLPHIN_XRA1405)
#if defined(ESP_PLATFORM)
  // ESP32
: m_pinDolphinSCK(18),
  m_pinDolphinCOPI(23),
  m_pinDolphinCIPO(19),
  m_pinDolphinCS(22),
  m_pinDolphinHandshakeTransmit(4),
  m_pinDolphinHandshakeReceive(36)
#elif defined(ARDUINO_ARCH_RP2040)
  // Raspberry Pi Pico
: m_pinDolphinCS(20),
  m_pinDolphinCIPO(16),
  m_pinDolphinCOPI(19),
  m_pinDolphinSCK(18),
  m_pinDolphinHandshakeTransmit(6),
  m_pinDolphinHandshakeReceive(15)
#elif defined(__AVR_ATmega328P__) || defined(ARDUINO_UNOR4)
  // Arduino UNO, Pro Mini, Micro, Nano
: m_pinDolphinCS(9),
  m_pinDolphinCIPO(12),
  m_pinDolphinCOPI(11),
  m_pinDolphinSCK(13),
  m_pinDolphinHandshakeTransmit(7),
  m_pinDolphinHandshakeReceive(2)
#else
#error "DolphinDos using XRA1405 not supported on this platform"
#endif
#else // !SUPPORT_DOLPHIN_XRA1405
#if defined(ESP_PLATFORM)
  // ESP32
: m_pinDolphinHandshakeTransmit(4),
  m_pinDolphinHandshakeReceive(36),
  m_pinDolphinParallel{13,14,15,16,17,25,26,27}
#elif defined(ARDUINO_ARCH_RP2040)
  // Raspberry Pi Pico
: m_pinDolphinHandshakeTransmit(6),
  m_pinDolphinHandshakeReceive(15),
  m_pinDolphinParallel{7,8,9,10,11,12,13,14}
#elif defined(__SAM3X8E__)
  // Arduino Due
: m_pinDolphinHandshakeTransmit(52),
  m_pinDolphinHandshakeReceive(53),
  m_pinDolphinParallel{51,50,49,48,47,46,45,44}
#elif defined(__AVR_ATmega328P__) || defined(ARDUINO_UNOR4)
  // Arduino UNO, Pro Mini, Nano
: m_pinDolphinHandshakeTransmit(7),
  m_pinDolphinHandshakeReceive(2),
  m_pinDolphinParallel{A0,A1,A2,A3,A4,A5,8,9}
#elif defined(__AVR_ATmega2560__)
  // Arduino Mega 2560
: m_pinDolphinHandshakeTransmit(30),
  m_pinDolphinHandshakeReceive(2),
  m_pinDolphinParallel{22,23,24,25,26,27,28,29}
#else
#error "DolphinDos not supported on this platform"
#endif
#endif // SUPPORT_DOLPHIN_XRA1405
#endif // SUPPORT_DOLPHIN
{
  m_numDevices = 0;
  m_inTask     = false;
  m_flags      = 0xFF; // 0xFF means: begin() has not yet been called

  m_pinATN       = pinATN;
  m_pinCLK       = pinCLK;
  m_pinDATA      = pinDATA;
  m_pinRESET     = pinRESET;
  m_pinCTRL      = pinCTRL;
  m_pinSRQ       = pinSRQ;

#if defined(SUPPORT_JIFFY) || defined(SUPPORT_EPYX) || defined(SUPPORT_DOLPHIN)
#if IEC_DEFAULT_FASTLOAD_BUFFER_SIZE>0
  m_bufferSize = IEC_DEFAULT_FASTLOAD_BUFFER_SIZE;
#else
  m_buffer = NULL;
  m_bufferSize = 0;
#endif
#endif

#ifdef IOREG_TYPE
  m_bitRESET     = digitalPinToBitMask(pinRESET);
  m_regRESETread = portInputRegister(digitalPinToPort(pinRESET));
  m_bitATN       = digitalPinToBitMask(pinATN);
  m_regATNread   = portInputRegister(digitalPinToPort(pinATN));
  m_bitCLK       = digitalPinToBitMask(pinCLK);
  m_regCLKread   = portInputRegister(digitalPinToPort(pinCLK));
  m_regCLKwrite  = portOutputRegister(digitalPinToPort(pinCLK));
  m_regCLKmode   = portModeRegister(digitalPinToPort(pinCLK));
  m_bitDATA      = digitalPinToBitMask(pinDATA);
  m_regDATAread  = portInputRegister(digitalPinToPort(pinDATA));
  m_regDATAwrite = portOutputRegister(digitalPinToPort(pinDATA));
  m_regDATAmode  = portModeRegister(digitalPinToPort(pinDATA));
#endif

  m_atnInterrupt = digitalPinToInterrupt(m_pinATN);
}


void IECBusHandler::begin()
{
  JDEBUGI();

  // set pins to output 0 (when in output mode)
  pinMode(m_pinCLK,  OUTPUT); digitalWrite(m_pinCLK, LOW); 
  pinMode(m_pinDATA, OUTPUT); digitalWrite(m_pinDATA, LOW); 

  pinMode(m_pinATN,   INPUT);
  pinMode(m_pinCLK,   INPUT);
  pinMode(m_pinDATA,  INPUT);
  if( m_pinCTRL<0xFF )  pinMode(m_pinCTRL,  OUTPUT);
  if( m_pinRESET<0xFF ) pinMode(m_pinRESET, INPUT);
  if( m_pinSRQ<0xFF )   pinMode(m_pinSRQ,   INPUT);
  m_flags = 0;

  // allow ATN to pull DATA low in hardware
  writePinCTRL(LOW);

  // if the ATN pin is capable of interrupts then use interrupts to detect 
  // ATN requests, otherwise we'll poll the ATN pin in function microTask().
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && s_bushandler==NULL )
    {
      s_bushandler = this;
      attachInterrupt(m_atnInterrupt, atnInterruptFcn, FALLING);
    }

  // call begin() function for all attached devices
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->begin();
}


bool IECBusHandler::canServeATN() 
{ 
  return (m_pinCTRL!=0xFF) || (m_atnInterrupt != NOT_AN_INTERRUPT); 
}


bool IECBusHandler::inTransaction()
{
  return (m_flags & (P_LISTENING|P_TALKING))!=0;
}


bool IECBusHandler::attachDevice(IECDevice *dev)
{
  if( m_numDevices<MAX_DEVICES && findDevice(dev->m_devnr, true)==NULL )
    {
      dev->m_handler = this;
      dev->m_sflags &= ~(S_JIFFY_DETECTED|S_JIFFY_BLOCK|S_DOLPHIN_DETECTED|S_DOLPHIN_BURST_TRANSMIT|S_DOLPHIN_BURST_RECEIVE|S_EPYX_HEADER|S_EPYX_LOAD|S_EPYX_SECTOROP);
#ifdef SUPPORT_DOLPHIN
      enableParallelPins();
#endif

      // if IECBusHandler::begin() has been called already then call the device's
      // begin() function now, otherwise it will be called in IECBusHandler::begin() 
      if( m_flags!=0xFF ) dev->begin();

      m_devices[m_numDevices] = dev;
      m_numDevices++;
      return true;
    }
  else
    return false;
}


bool IECBusHandler::detachDevice(IECDevice *dev)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( dev == m_devices[i] )
      {
        dev->m_handler = NULL;
        m_devices[i] = m_devices[m_numDevices-1];
        m_numDevices--;
#ifdef SUPPORT_DOLPHIN
        enableParallelPins();
#endif
        return true;
      }

  return false;
}


IECDevice *IECBusHandler::findDevice(uint8_t devnr, bool includeInactive)
{
  for(uint8_t i=0; i<m_numDevices; i++)
    if( devnr == m_devices[i]->m_devnr && (includeInactive || m_devices[i]->isActive()) )
      return m_devices[i];

  return NULL;
}


void IRAM_ATTR IECBusHandler::atnInterruptFcn(INTERRUPT_FCN_ARG)
{ 
  if( s_bushandler!=NULL && !s_bushandler->m_inTask & ((s_bushandler->m_flags & P_ATN)==0) )
    s_bushandler->atnRequest();
}


#if (defined(SUPPORT_JIFFY) || defined(SUPPORT_DOLPHIN) || defined(SUPPORT_EPYX)) && !defined(IEC_DEFAULT_FASTLOAD_BUFFER_SIZE)
void IECBusHandler::setBuffer(uint8_t *buffer, uint8_t bufferSize)
{
  m_buffer     = bufferSize>0 ? buffer : NULL;
  m_bufferSize = bufferSize;
}
#endif

#ifdef SUPPORT_JIFFY

// ------------------------------------  JiffyDos support routines  ------------------------------------  


bool IECBusHandler::enableJiffyDosSupport(IECDevice *dev, bool enable)
{
  if( enable && m_bufferSize>0 )
    dev->m_sflags |= S_JIFFY_ENABLED;
  else
    dev->m_sflags &= ~S_JIFFY_ENABLED;

  // cancel any current JiffyDos activities
  dev->m_sflags &= ~(S_JIFFY_DETECTED|S_JIFFY_BLOCK);

  return (dev->m_sflags & S_JIFFY_ENABLED)!=0;
}


bool IRAM_ATTR IECBusHandler::receiveJiffyByte(bool canWriteOk)
{
  uint8_t data = 0;
  JDEBUG1();
  timer_init();
  timer_reset();

  noInterrupts(); 

  // signal "ready" by releasing DATA
  writePinDATA(HIGH);

  // wait (indefinitely) for either CLK high ("ready-to-send") or ATN low
  // NOTE: this must be in a blocking loop since the sender starts transmitting
  // the byte immediately after setting CLK high. If we exit the "task" function then
  // we may not get back here in time to receive.
#ifdef ESP_PLATFORM
  while( !digitalReadFastExt(m_pinCLK, m_regCLKread, m_bitCLK) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) )
    if( !timer_less_than(IWDT_FEED_TIME) )
      {
        // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
        interrupts(); noInterrupts();
        timer_reset();
      }
#else
  while( !digitalReadFastExt(m_pinCLK, m_regCLKread, m_bitCLK) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) );
#endif

  // start timer (on AVR, lag from CLK high to timer start is between 700...1700ns)
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() )
    { interrupts(); return false; }

  // bits 4+5 are set by sender 11 cycles after CLK HIGH (FC51)
  // wait until 14us after CLK
  timer_wait_until(14);
  
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(4);
  if( !readPinDATA() ) data |= bit(5);
  JDEBUG0();

  // bits 6+7 are set by sender 24 cycles after CLK HIGH (FC5A)
  // wait until 27us after CLK
  timer_wait_until(27);
  
  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(6);
  if( !readPinDATA() ) data |= bit(7);
  JDEBUG0();

  // bits 3+1 are set by sender 35 cycles after CLK HIGH (FC62)
  // wait until 38us after CLK
  timer_wait_until(38);

  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(3);
  if( !readPinDATA() ) data |= bit(1);
  JDEBUG0();

  // bits 2+0 are set by sender 48 cycles after CLK HIGH (FC6B)
  // wait until 51us after CLK
  timer_wait_until(51);

  JDEBUG1();
  if( !readPinCLK()  ) data |= bit(2);
  if( !readPinDATA() ) data |= bit(0);
  JDEBUG0();

  // sender sets EOI status 61 cycles after CLK HIGH (FC76)
  // wait until 64us after CLK
  timer_wait_until(64);

  // if CLK is high at this point then the sender is signaling EOI
  JDEBUG1();
  bool eoi = readPinCLK();

  // acknowledge receipt
  writePinDATA(LOW);

  // sender reads acknowledgement 80 cycles after CLK HIGH (FC82)
  // wait until 83us after CLK
  timer_wait_until(83);

  JDEBUG0();

  interrupts();

  if( canWriteOk )
    {
      // pass received data on to the device
      m_currentDevice->write(data, eoi);
    }
  else
    {
      // canWrite() reported an error
      return false;
    }
  
  return true;
}


bool IRAM_ATTR IECBusHandler::transmitJiffyByte(uint8_t numData)
{
  uint8_t data = numData>0 ? m_currentDevice->peek() : 0;

  JDEBUG1();
  timer_init();
  timer_reset();

  noInterrupts();

  // signal "READY" by releasing CLK
  writePinCLK(HIGH);
  
  // wait (indefinitely) for either DATA high ("ready-to-receive", FBCB) or ATN low
  // NOTE: this must be in a blocking loop since the receiver receives the data
  // immediately after setting DATA high. If we exit the "task" function then
  // we may not get back here in time to transmit.
#ifdef ESP_PLATFORM
  while( !digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) )
    if( !timer_less_than(IWDT_FEED_TIME) )
      {
        // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
        interrupts(); noInterrupts();
        timer_reset();
      }
#else
  while( !digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) );
#endif

  // start timer (on AVR, lag from DATA high to timer start is between 700...1700ns)
  timer_start();
  JDEBUG0();

  // abort if ATN low
  if( !readPinATN() )
    { interrupts(); return false; }

  writePinCLK(data & bit(0));
  writePinDATA(data & bit(1));
  JDEBUG1();
  // bits 0+1 are read by receiver 16 cycles after DATA HIGH (FBD5)

  // wait until 16.5 us after DATA
  timer_wait_until(16.5);
  
  JDEBUG0();
  writePinCLK(data & bit(2));
  writePinDATA(data & bit(3));
  JDEBUG1();
  // bits 2+3 are read by receiver 26 cycles after DATA HIGH (FBDB)

  // wait until 27.5 us after DATA
  timer_wait_until(27.5);

  JDEBUG0();
  writePinCLK(data & bit(4));
  writePinDATA(data & bit(5));
  JDEBUG1();
  // bits 4+5 are read by receiver 37 cycles after DATA HIGH (FBE2)

  // wait until 39 us after DATA
  timer_wait_until(39);

  JDEBUG0();
  writePinCLK(data & bit(6));
  writePinDATA(data & bit(7));
  JDEBUG1();
  // bits 6+7 are read by receiver 48 cycles after DATA HIGH (FBE9)

  // wait until 50 us after DATA
  timer_wait_until(50);
  JDEBUG0();
      
  // numData:
  //   0: no data was available to read (error condition, discard this byte)
  //   1: this was the last byte of data
  //  >1: more data is available after this
  if( numData>1 )
    {
      // CLK=LOW  and DATA=HIGH means "at least one more byte"
      writePinCLK(LOW);
      writePinDATA(HIGH);
    }
  else
    {
      // CLK=HIGH and DATA=LOW  means EOI (this was the last byte)
      // CLK=HIGH and DATA=HIGH means "error"
      writePinCLK(HIGH);
      writePinDATA(numData==0);
    }

  // EOI/error status is read by receiver 59 cycles after DATA HIGH (FBEF)
  // receiver sets DATA low 63 cycles after initial DATA HIGH (FBF2)
  timer_wait_until(60);

  // receiver signals "done" by pulling DATA low (FBF2)
  JDEBUG1();
  if( !waitPinDATA(LOW) ) { interrupts(); return false; }
  JDEBUG0();

  interrupts();

  if( numData>0 )
    {
      // success => discard transmitted byte (was previously read via peek())
      m_currentDevice->read();
      return true;
    }
  else
    return false;
}


bool IRAM_ATTR IECBusHandler::transmitJiffyBlock(uint8_t *buffer, uint8_t numBytes)
{
  JDEBUG1();
  timer_init();

  // wait (indefinitely) until receiver is not holding DATA low anymore (FB07)
  // NOTE: this must be in a blocking loop since the receiver starts counting
  // up the EOI timeout immediately after setting DATA HIGH. If we had exited the 
  // "task" function then it might be more than 200us until we get back here
  // to pull CLK low and the receiver will interpret that delay as EOI.
  while( !readPinDATA() )
    if( !readPinATN() )
      { JDEBUG0(); return false; }

  // receiver will be in "new data block" state at this point,
  // waiting for us (FB0C) to release CLK
  if( numBytes==0 )
    {
      // nothing to send => signal EOI by keeping DATA high
      // and pulsing CLK high-low
      writePinDATA(HIGH);
      writePinCLK(HIGH);
      if( !waitTimeout(100) ) return false;
      writePinCLK(LOW);
      if( !waitTimeout(100) ) return false;
      JDEBUG0(); 
      return false;
    }

  // signal "ready to send" by pulling DATA low and releasing CLK
  writePinDATA(LOW);
  writePinCLK(HIGH);

  // delay to make sure receiver has seen DATA=LOW - even though receiver 
  // is in a tight loop (at FB0C), a VIC "bad line" may steal 40-50us.
  if( !waitTimeout(60) ) return false;

  noInterrupts();

  for(uint8_t i=0; i<numBytes; i++)
    {
      uint8_t data = buffer[i];

      // release DATA
      writePinDATA(HIGH);

      // stop and reset timer
      timer_stop();
      timer_reset();

      // signal READY by releasing CLK
      writePinCLK(HIGH);

      // make sure DATA has settled on HIGH
      // (receiver takes at least 19 cycles between seeing DATA HIGH [at FB3E] and setting DATA LOW [at FB51]
      // so waiting a couple microseconds will not hurt transfer performance)
      delayMicrosecondsISafe(2);

      // wait (indefinitely) for either DATA low (FB51) or ATN low
      // NOTE: this must be in a blocking loop since the receiver receives the data
      // immediately after setting DATA high. If we exit the "task" function then
      // we may not get back here in time to transmit.
#ifdef ESP_PLATFORM
      while( digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) )
        if( !timer_less_than(IWDT_FEED_TIME) )
          {
            // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
            interrupts(); noInterrupts();
            timer_reset();
          }
#else
      while( digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) );
#endif

      // start timer (on AVR, lag from DATA low to timer start is between 700...1700ns)
      timer_start();
      JDEBUG0();
      
      // abort if ATN low
      if( !readPinATN() )
        { interrupts(); return false; }

      // receiver expects to see CLK high at 4 cycles after DATA LOW (FB54)
      // wait until 6 us after DATA LOW
      timer_wait_until(6);

      JDEBUG0();
      writePinCLK(data & bit(0));
      writePinDATA(data & bit(1));
      JDEBUG1();
      // bits 0+1 are read by receiver 16 cycles after DATA LOW (FB5D)

      // wait until 17 us after DATA LOW
      timer_wait_until(17);
  
      JDEBUG0();
      writePinCLK(data & bit(2));
      writePinDATA(data & bit(3));
      JDEBUG1();
      // bits 2+3 are read by receiver 26 cycles after DATA LOW (FB63)

      // wait until 27 us after DATA LOW
      timer_wait_until(27);

      JDEBUG0();
      writePinCLK(data & bit(4));
      writePinDATA(data & bit(5));
      JDEBUG1();
      // bits 4+5 are read by receiver 37 cycles after DATA LOW (FB6A)

      // wait until 39 us after DATA LOW
      timer_wait_until(39);

      JDEBUG0();
      writePinCLK(data & bit(6));
      writePinDATA(data & bit(7));
      JDEBUG1();
      // bits 6+7 are read by receiver 48 cycles after DATA LOW (FB71)

      // wait until 50 us after DATA LOW
      timer_wait_until(50);
    }

  // signal "not ready" by pulling CLK LOW
  writePinCLK(LOW);

  // release DATA
  writePinDATA(HIGH);

  interrupts();

  JDEBUG0();

  return true;
}


#endif // !SUPPORT_JIFFY


#ifdef SUPPORT_DOLPHIN

// ------------------------------------  DolphinDos support routines  ------------------------------------  

#define DOLPHIN_PREBUFFER_BYTES 2

#ifdef SUPPORT_DOLPHIN_XRA1405

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#include "IECespidf-spi.h"
#else
#include "SPI.h"
#endif

#pragma GCC push_options
#pragma GCC optimize ("O2")

uint8_t IRAM_ATTR IECBusHandler::XRA1405_ReadReg(uint8_t reg)
{
  startParallelTransaction();
  digitalWriteFastExt(m_pinDolphinCS, m_regDolphinCS, m_bitDolphinCS, LOW);
  uint8_t res = SPI.transfer16((0x40|reg) << 9) & 0xFF;
  digitalWriteFastExt(m_pinDolphinCS, m_regDolphinCS, m_bitDolphinCS, HIGH);
  endParallelTransaction();
  return res;
}

void IRAM_ATTR IECBusHandler::XRA1405_WriteReg(uint8_t reg, uint8_t data)
{
  startParallelTransaction();
  digitalWriteFastExt(m_pinDolphinCS, m_regDolphinCS, m_bitDolphinCS, LOW);
  SPI.transfer16((reg << 9) | data);
  digitalWriteFastExt(m_pinDolphinCS, m_regDolphinCS, m_bitDolphinCS, HIGH);
  endParallelTransaction();
}

#pragma GCC pop_options

#endif

#if defined(ESP_PLATFORM) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))

#include <driver/pulse_cnt.h>
pcnt_unit_handle_t esp32_pulse_count_unit = NULL;
pcnt_channel_handle_t esp32_pulse_count_channel = NULL;
volatile static bool _handshakeReceived = false;
static bool handshakeIRQ(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)  { _handshakeReceived = true; return false; }
#define DOLPHIN_HANDSHAKE_USE_INTERRUPT

#elif !defined(__AVR_ATmega328P__) && !defined(__AVR_ATmega2560__)

volatile static bool _handshakeReceived = false;
static void IRAM_ATTR handshakeIRQ(INTERRUPT_FCN_ARG) { _handshakeReceived = true; }
#define DOLPHIN_HANDSHAKE_USE_INTERRUPT

#endif


#ifdef DOLPHIN_HANDSHAKE_USE_INTERRUPT

bool IRAM_ATTR IECBusHandler::parallelCableDetect()
{
  // DolphinDos cable detection happens at the end of the ATN sequence
  // during which interrupts are disabled so we can't use parallelBusHandshakeReceived()
  // which relies on the IRQ handler above

#if defined(IOREG_TYPE)
  volatile const IOREG_TYPE *regHandshakeReceive = portInputRegister(digitalPinToPort(m_pinDolphinHandshakeReceive));
  volatile IOREG_TYPE bitHandshakeReceive = digitalPinToBitMask(m_pinDolphinHandshakeReceive);
#endif

  // wait for handshake signal going LOW until ATN goes high
  while( !digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) )
    {
      if( !digitalReadFastExt(m_pinDolphinHandshakeReceive, regHandshakeReceive, bitHandshakeReceive) ) return true;
      if( !digitalReadFastExt(m_pinDolphinHandshakeReceive, regHandshakeReceive, bitHandshakeReceive) ) return true;
      if( !digitalReadFastExt(m_pinDolphinHandshakeReceive, regHandshakeReceive, bitHandshakeReceive) ) return true;
    }

  return false;
}

#else

bool IRAM_ATTR IECBusHandler::parallelCableDetect()
{
  // clear any previous handshakes
  parallelBusHandshakeReceived();

  // wait for handshake
  while( !readPinATN() )
    if( parallelBusHandshakeReceived() )
      return true;

  return false;
}


#endif

#ifdef SUPPORT_DOLPHIN_XRA1405

void IECBusHandler::setDolphinDosPins(uint8_t pinHT, uint8_t pinHR, uint8_t pinSCK, uint8_t pinCOPI, uint8_t pinCIPO, uint8_t pinCS)
{
  m_pinDolphinHandshakeTransmit = pinHT;
  m_pinDolphinHandshakeReceive  = pinHR;
  m_pinDolphinCOPI = pinCOPI;
  m_pinDolphinCIPO = pinCIPO;
  m_pinDolphinSCK  = pinSCK;
  m_pinDolphinCS   = pinCS;
}

#else

void IECBusHandler::setDolphinDosPins(uint8_t pinHT, uint8_t pinHR,uint8_t pinD0, uint8_t pinD1, uint8_t pinD2, uint8_t pinD3, uint8_t pinD4, uint8_t pinD5, uint8_t pinD6, uint8_t pinD7)
{
  m_pinDolphinHandshakeTransmit = pinHT;
  m_pinDolphinHandshakeReceive  = pinHR;
  m_pinDolphinParallel[0] = pinD0;
  m_pinDolphinParallel[1] = pinD1;
  m_pinDolphinParallel[2] = pinD2;
  m_pinDolphinParallel[3] = pinD3;
  m_pinDolphinParallel[4] = pinD4;
  m_pinDolphinParallel[5] = pinD5;
  m_pinDolphinParallel[6] = pinD6;
  m_pinDolphinParallel[7] = pinD7;
}

#endif

bool IECBusHandler::enableDolphinDosSupport(IECDevice *dev, bool enable)
{
  if( enable && m_bufferSize>=DOLPHIN_PREBUFFER_BYTES && 
      !isDolphinPin(m_pinATN)   && !isDolphinPin(m_pinCLK) && !isDolphinPin(m_pinDATA) && 
      !isDolphinPin(m_pinRESET) && !isDolphinPin(m_pinCTRL) && 
#ifdef SUPPORT_DOLPHIN_XRA1405
      m_pinDolphinCS!=0xFF && m_pinDolphinSCK!=0xFF && m_pinDolphinCOPI!=0xFF && m_pinDolphinCIPO!=0xFF &&
#else
      m_pinDolphinParallel[0]!=0xFF && m_pinDolphinParallel[1]!=0xFF &&
      m_pinDolphinParallel[2]!=0xFF && m_pinDolphinParallel[3]!=0xFF &&
      m_pinDolphinParallel[4]!=0xFF && m_pinDolphinParallel[5]!=0xFF &&
      m_pinDolphinParallel[6]!=0xFF && m_pinDolphinParallel[6]!=0xFF &&
#endif
      m_pinDolphinHandshakeTransmit!=0xFF && m_pinDolphinHandshakeReceive!=0xFF && 
      digitalPinToInterrupt(m_pinDolphinHandshakeReceive)!=NOT_AN_INTERRUPT )
    {
      dev->m_sflags |= S_DOLPHIN_ENABLED|S_DOLPHIN_BURST_ENABLED;
    }
  else
    dev->m_sflags &= ~(S_DOLPHIN_ENABLED|S_DOLPHIN_BURST_ENABLED);

  // cancel any current DolphinDos activities
  dev->m_sflags &= ~(S_DOLPHIN_DETECTED|S_DOLPHIN_BURST_TRANSMIT|S_DOLPHIN_BURST_RECEIVE);

  // make sure pins for parallel cable are enabled/disabled as needed
  enableParallelPins();

  return (dev->m_sflags & S_DOLPHIN_ENABLED)!=0;
}


bool IECBusHandler::isDolphinPin(uint8_t pin)
{
  if( pin==m_pinDolphinHandshakeTransmit || pin==m_pinDolphinHandshakeReceive )
    return true;

#ifdef SUPPORT_DOLPHIN_XRA1405
  if( pin==m_pinDolphinCS || pin==m_pinDolphinCOPI || pin==m_pinDolphinCIPO || pin==m_pinDolphinSCK )
    return true;
#else
  for(int i=0; i<8; i++) 
    if( pin==m_pinDolphinParallel[i] )
      return true;
#endif

  return false;
}


void IECBusHandler::enableParallelPins()
{
  uint8_t i = 0;
  for(i=0; i<m_numDevices; i++)
    if( m_devices[i]->m_sflags & S_DOLPHIN_ENABLED )
      break;

  if( i<m_numDevices )
    {
      // at least one device has DolphinDos support enabled

#if defined(IOREG_TYPE)
      m_regDolphinHandshakeTransmitMode = portModeRegister(digitalPinToPort(m_pinDolphinHandshakeTransmit));
      m_bitDolphinHandshakeTransmit     = digitalPinToBitMask(m_pinDolphinHandshakeTransmit);
#if defined(SUPPORT_DOLPHIN_XRA1405)
      m_regDolphinCS = portOutputRegister(digitalPinToPort(m_pinDolphinCS));
      m_bitDolphinCS = digitalPinToBitMask(m_pinDolphinCS);
#else
      for(uint8_t i=0; i<8; i++)
        {
          m_regDolphinParallelWrite[i] = portOutputRegister(digitalPinToPort(m_pinDolphinParallel[i]));
          m_regDolphinParallelRead[i]  = portInputRegister(digitalPinToPort(m_pinDolphinParallel[i]));
          m_regDolphinParallelMode[i]  = portModeRegister(digitalPinToPort(m_pinDolphinParallel[i]));
          m_bitDolphinParallel[i]      = digitalPinToBitMask(m_pinDolphinParallel[i]);
        }
#endif
#endif
      // initialize handshake transmit (high-Z)
      pinMode(m_pinDolphinHandshakeTransmit, OUTPUT);
      digitalWrite(m_pinDolphinHandshakeTransmit, LOW);
      pinModeFastExt(m_pinDolphinHandshakeTransmit, m_regDolphinHandshakeTransmitMode, m_bitDolphinHandshakeTransmit, INPUT);
      
      // initialize handshake receive (using INPUT_PULLUP to avoid undefined behavior
      // when parallel cable is not connected)
      pinMode(m_pinDolphinHandshakeReceive, INPUT_PULLUP);

      // For 8-bit AVR platforms (Arduino Uno R3, Arduino Mega) the interrupt latency combined
      // with the comparatively slow clock speed leads to reduced performance during load/save
      // For those platforms we do not use the generic interrupt mechanism but instead directly 
      // access the registers dealing with external interrupts.
      // All other platforms are fast enough so we can use the interrupt mechanism without
      // performance issues.
#if defined(__AVR_ATmega328P__)
      // 
      if( m_pinDolphinHandshakeReceive==2 )
        {
          EIMSK &= ~bit(INT0);  // disable pin change interrupt
          EICRA &= ~bit(ISC00); EICRA |=  bit(ISC01); // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF0);
        }
      else if( m_pinDolphinHandshakeReceive==3 )
        {
          EIMSK &= ~bit(INT1);  // disable pin change interrupt
          EICRA &= ~bit(ISC10); EICRA |=  bit(ISC11); // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF1);
        }
#elif defined(__AVR_ATmega2560__)
      if( m_pinDolphinHandshakeReceive==2 )
        {
          EIMSK &= ~bit(INT4); // disable interrupt
          EICRB &= ~bit(ISC40); EICRB |=  bit(ISC41);  // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF4);
        }
      else if( m_pinDolphinHandshakeReceive==3 )
        {
          EIMSK &= ~bit(INT5); // disable interrupt
          EICRB &= ~bit(ISC50); EICRB |=  bit(ISC51);  // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF5);
        }
      else if( m_pinDolphinHandshakeReceive==18 )
        {
          EIMSK &= ~bit(INT3); // disable interrupt
          EICRA &= ~bit(ISC30); EICRA |=  bit(ISC31);  // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF3);
        }
      else if( m_pinDolphinHandshakeReceive==19 )
        {
          EIMSK &= ~bit(INT2); // disable interrupt
          EICRA &= ~bit(ISC20); EICRA |=  bit(ISC21);  // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF2);
        }
      else if( m_pinDolphinHandshakeReceive==20 )
        {
          EIMSK &= ~bit(INT1); // disable interrupt
          EICRA &= ~bit(ISC10); EICRA |=  bit(ISC11);  // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF1);
        }
      else if( m_pinDolphinHandshakeReceive==21 )
        {
          EIMSK &= ~bit(INT0); // disable interrupt
          EICRA &= ~bit(ISC00); EICRA |=  bit(ISC01);  // enable falling edge detection
          m_handshakeReceivedBit = bit(INTF0);
        }
#elif defined(ESP_PLATFORM) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
      // use pulse counter on handshake receive line (utilizing its glitch filter)
      if( esp32_pulse_count_unit==NULL )
        {
          pcnt_unit_config_t unit_config = {.low_limit = -1, .high_limit = 1, .flags = 0};
          ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &esp32_pulse_count_unit));
          pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 250 };
          ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(esp32_pulse_count_unit, &filter_config));
          pcnt_chan_config_t chan_config;
          memset(&chan_config, 0, sizeof(pcnt_chan_config_t));
          chan_config.edge_gpio_num = m_pinDolphinHandshakeReceive;
          chan_config.level_gpio_num = -1;
          ESP_ERROR_CHECK(pcnt_new_channel(esp32_pulse_count_unit, &chan_config, &esp32_pulse_count_channel));
          ESP_ERROR_CHECK(pcnt_channel_set_edge_action(esp32_pulse_count_channel, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
          pcnt_event_callbacks_t cbs = { .on_reach = handshakeIRQ };
          ESP_ERROR_CHECK(pcnt_unit_add_watch_point(esp32_pulse_count_unit, 1));
          ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(esp32_pulse_count_unit, &cbs, NULL));
          ESP_ERROR_CHECK(pcnt_unit_enable(esp32_pulse_count_unit));
          ESP_ERROR_CHECK(pcnt_unit_clear_count(esp32_pulse_count_unit));
          ESP_ERROR_CHECK(pcnt_unit_start(esp32_pulse_count_unit));
        }
#elif defined(DOLPHIN_HANDSHAKE_USE_INTERRUPT)
      attachInterrupt(digitalPinToInterrupt(m_pinDolphinHandshakeReceive), handshakeIRQ, FALLING);
#endif

      // initialize parallel bus pins
#ifdef SUPPORT_DOLPHIN_XRA1405
      digitalWrite(m_pinDolphinCS, HIGH);
      pinMode(m_pinDolphinCS, OUTPUT);
      digitalWrite(m_pinDolphinCS, HIGH);
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
      // for ESP32 ESPIDF, SPI settings are specified in "begin()" instead of "beginTransaction()"
      // (we use 16MHz since at 26MHz we run into timing issues during receive, the frequency
      // does not matter too much since we only send 16 bits of data at a time)
      SPI.begin(m_pinDolphinSCK, m_pinDolphinCIPO, m_pinDolphinCOPI, SPISettings(16000000, MSBFIRST, SPI_MODE0));
#elif defined(ESP_PLATFORM) && defined(ARDUINO)
      // SPI for ESP32 under Arduino requires pin assignments in "begin" call
      SPI.begin(m_pinDolphinSCK, m_pinDolphinCIPO, m_pinDolphinCOPI);
#else
      SPI.begin();
#endif
      setParallelBusModeInput();
      m_inTransaction = 0;
#else
      for(int i=0; i<8; i++) pinMode(m_pinDolphinParallel[i], INPUT);
#endif
    }
  else
    {
#if defined(ESP_PLATFORM) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
      // disable pulse counter on handshake receive line
      if( esp32_pulse_count_unit!=NULL )
        {
          pcnt_unit_stop(esp32_pulse_count_unit);
          pcnt_unit_disable(esp32_pulse_count_unit);
          pcnt_del_channel(esp32_pulse_count_channel);
          pcnt_del_unit(esp32_pulse_count_unit);
          esp32_pulse_count_unit = NULL;
          esp32_pulse_count_channel = NULL;
        }
#elif defined(DOLPHIN_HANDSHAKE_USE_INTERRUPT)
      detachInterrupt(digitalPinToInterrupt(m_pinDolphinHandshakeReceive));
#endif
    }
}


bool IRAM_ATTR IECBusHandler::parallelBusHandshakeReceived()
{
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega2560__)
  // see comment in function enableDolphinDosSupport
  if( EIFR & m_handshakeReceivedBit )
    {
      EIFR |= m_handshakeReceivedBit;
      return true;
    }
  else
    return false;
#else
  if( _handshakeReceived )
    {
      _handshakeReceived = false;
      return true;
    }
  else
    return false;
#endif
}


void IRAM_ATTR IECBusHandler::parallelBusHandshakeTransmit()
{
  // Emulate open collector behavior: 
  // - switch pin to INPUT  mode (high-Z output) for true
  // - switch pun to OUTPUT mode (LOW output) for false
  pinModeFastExt(m_pinDolphinHandshakeTransmit, m_regDolphinHandshakeTransmitMode, m_bitDolphinHandshakeTransmit, OUTPUT);
  delayMicrosecondsISafe(1);
  pinModeFastExt(m_pinDolphinHandshakeTransmit, m_regDolphinHandshakeTransmitMode, m_bitDolphinHandshakeTransmit, INPUT);
}


void IRAM_ATTR IECBusHandler::startParallelTransaction()
{
#ifdef SUPPORT_DOLPHIN_XRA1405
  if( m_inTransaction==0 )
    {
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
      // for ESPIDF, SPI settings are specified in "begin()" instead of "beginTransaction()"
      SPI.beginTransaction();
#else
      SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
#endif
    }

  m_inTransaction++;
#endif
}


void IRAM_ATTR IECBusHandler::endParallelTransaction()
{
#ifdef SUPPORT_DOLPHIN_XRA1405
  if( m_inTransaction==1 ) SPI.endTransaction();
  if( m_inTransaction>0  ) m_inTransaction--;
#endif
}


#pragma GCC push_options
#pragma GCC optimize ("O2")
uint8_t IRAM_ATTR IECBusHandler::readParallelData()
{
  uint8_t res = 0;
#ifdef SUPPORT_DOLPHIN_XRA1405
  res = XRA1405_ReadReg(0x00); // GSR1, GPIO State Register for P0-P7
#else
  // loop unrolled for performance
  if( digitalReadFastExt(m_pinDolphinParallel[0], m_regDolphinParallelRead[0], m_bitDolphinParallel[0]) ) res |= 0x01;
  if( digitalReadFastExt(m_pinDolphinParallel[1], m_regDolphinParallelRead[1], m_bitDolphinParallel[1]) ) res |= 0x02;
  if( digitalReadFastExt(m_pinDolphinParallel[2], m_regDolphinParallelRead[2], m_bitDolphinParallel[2]) ) res |= 0x04;
  if( digitalReadFastExt(m_pinDolphinParallel[3], m_regDolphinParallelRead[3], m_bitDolphinParallel[3]) ) res |= 0x08;
  if( digitalReadFastExt(m_pinDolphinParallel[4], m_regDolphinParallelRead[4], m_bitDolphinParallel[4]) ) res |= 0x10;
  if( digitalReadFastExt(m_pinDolphinParallel[5], m_regDolphinParallelRead[5], m_bitDolphinParallel[5]) ) res |= 0x20;
  if( digitalReadFastExt(m_pinDolphinParallel[6], m_regDolphinParallelRead[6], m_bitDolphinParallel[6]) ) res |= 0x40;
  if( digitalReadFastExt(m_pinDolphinParallel[7], m_regDolphinParallelRead[7], m_bitDolphinParallel[7]) ) res |= 0x80;
#endif
  return res;
}


void IRAM_ATTR IECBusHandler::writeParallelData(uint8_t data)
{
#ifdef SUPPORT_DOLPHIN_XRA1405
  XRA1405_WriteReg(0x02, data); // OCR1, GPIO Output Control Register for P0-P7
#else
  // loop unrolled for performance
  digitalWriteFastExt(m_pinDolphinParallel[0], m_regDolphinParallelWrite[0], m_bitDolphinParallel[0], data & 0x01);
  digitalWriteFastExt(m_pinDolphinParallel[1], m_regDolphinParallelWrite[1], m_bitDolphinParallel[1], data & 0x02);
  digitalWriteFastExt(m_pinDolphinParallel[2], m_regDolphinParallelWrite[2], m_bitDolphinParallel[2], data & 0x04);
  digitalWriteFastExt(m_pinDolphinParallel[3], m_regDolphinParallelWrite[3], m_bitDolphinParallel[3], data & 0x08);
  digitalWriteFastExt(m_pinDolphinParallel[4], m_regDolphinParallelWrite[4], m_bitDolphinParallel[4], data & 0x10);
  digitalWriteFastExt(m_pinDolphinParallel[5], m_regDolphinParallelWrite[5], m_bitDolphinParallel[5], data & 0x20);
  digitalWriteFastExt(m_pinDolphinParallel[6], m_regDolphinParallelWrite[6], m_bitDolphinParallel[6], data & 0x40);
  digitalWriteFastExt(m_pinDolphinParallel[7], m_regDolphinParallelWrite[7], m_bitDolphinParallel[7], data & 0x80);
#endif
}


void IRAM_ATTR IECBusHandler::setParallelBusModeInput()
{
#ifdef SUPPORT_DOLPHIN_XRA1405
  XRA1405_WriteReg(0x06, 0xFF); // GCR1, GPIO Configuration Register for P0-P7
#else
  // set parallel bus data pins to input mode
  for(int i=0; i<8; i++) 
    pinModeFastExt(m_pinDolphinParallel[i], m_regDolphinParallelMode[i], m_bitDolphinParallel[i], INPUT);
#endif
}


void IRAM_ATTR IECBusHandler::setParallelBusModeOutput()
{
#ifdef SUPPORT_DOLPHIN_XRA1405
  XRA1405_WriteReg(0x06, 0x00); // GCR1, GPIO Configuration Register for P0-P7
#else
  // set parallel bus data pins to output mode
  for(int i=0; i<8; i++) 
    pinModeFastExt(m_pinDolphinParallel[i], m_regDolphinParallelMode[i], m_bitDolphinParallel[i], OUTPUT);
#endif
}
#pragma GCC pop_options


bool IECBusHandler::waitParallelBusHandshakeReceived()
{
  while( !parallelBusHandshakeReceived() )
    if( !readPinATN() )
      return false;

  return true;
}


void IECBusHandler::enableDolphinBurstMode(IECDevice *dev, bool enable)
{
  if( enable )
    dev->m_sflags |= S_DOLPHIN_BURST_ENABLED;
  else
    dev->m_sflags &= ~S_DOLPHIN_BURST_ENABLED;

  dev->m_sflags &= ~(S_DOLPHIN_BURST_TRANSMIT|S_DOLPHIN_BURST_RECEIVE);
}

void IECBusHandler::dolphinBurstReceiveRequest(IECDevice *dev)
{
  dev->m_sflags |= S_DOLPHIN_BURST_RECEIVE;
  m_timeoutStart = micros();
}

void IECBusHandler::dolphinBurstTransmitRequest(IECDevice *dev)
{
  dev->m_sflags |= S_DOLPHIN_BURST_TRANSMIT;
  m_timeoutStart = micros();
}

bool IECBusHandler::receiveDolphinByte(bool canWriteOk)
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by releasing CLK
  bool eoi = false;

  // we have buffered bytes (see comment below) that need to be
  // sent on to the higher level handler before we can receive more.
  // There are two ways to get to m_dolphinCtr==DOLPHIN_PREBUFFER_BYTES:
  // 1) the host never sends a XZ burst request and just keeps sending data
  // 2) the host sends a burst request but we reject it
  // note that we must wait for the host to be ready to send the next data 
  // byte before we can empty our buffer, otherwise we will already empty
  // it before the host sends the burst (XZ) request
  if( m_secondary==0x61 && m_dolphinCtr > 0 && m_dolphinCtr <= DOLPHIN_PREBUFFER_BYTES )
    {
      // send next buffered byte on to higher level
      m_currentDevice->write(m_buffer[m_dolphinCtr-1], false);
      m_dolphinCtr--;
      return true;
    }

  noInterrupts();

  // signal "ready"
  writePinDATA(HIGH);

  // wait for CLK low
  if( !waitPinCLK(LOW, 100) )
    {
      // exit if waitPinCLK returned because of falling edge on ATN
      if( !readPinATN() ) { interrupts(); return false; }

      // sender did not set CLK low within 100us after we set DATA high
      // => it is signaling EOI
      // acknowledge we received it by setting DATA low for 60us
      eoi = true;
      writePinDATA(LOW);
      if( !waitTimeout(60) ) { interrupts(); return false; }
      writePinDATA(HIGH);

      // keep waiting for CLK low
      if( !waitPinCLK(LOW) ) { interrupts(); return false; }
    }

  // get data
  if( canWriteOk )
    {
      // read data from parallel bus
      uint8_t data = readParallelData();

      // confirm receipt
      writePinDATA(LOW);

      interrupts();

      // when executing a SAVE command, DolphinDos first sends two bytes of data,
      // and then the "XZ" burst request. If the transmission happens in burst mode then
      // that data is going to be sent again and the initial data is discarded.
      // (MultiDubTwo actually sends garbage bytes for the initial two bytes)
      // so we can't pass the first two bytes on yet because we don't yet know if this is
      // going to be a burst transmission. If it is NOT a burst then we need to send them
      // later (see beginning of this function). If it is a burst then we discard them.
      // Note that the SAVE command always operates on channel 1 (secondary address 0x61)
      // so we only do the buffering in that case. 
      if( m_secondary==0x61 && m_dolphinCtr > DOLPHIN_PREBUFFER_BYTES )
        {
          m_buffer[m_dolphinCtr-DOLPHIN_PREBUFFER_BYTES-1] = data;
          m_dolphinCtr--;
        }
      else
        {
          // pass received data on to the device
          m_currentDevice->write(data, eoi);
        }

      return true;
    }
  else
    {
      // canWrite reported an error
      interrupts();
      return false;
    }
}


bool IECBusHandler::transmitDolphinByte(uint8_t numData)
{
  // Note: receiver starts a 50us timeout after setting DATA high
  // (ready-to-receive) waiting for CLK low (data valid). If we take
  // longer than those 50us the receiver will interpret that as EOI
  // (last byte of data). So we need to take precautions:
  // - disable interrupts between setting CLK high and setting CLK low
  // - get the data byte to send before setting CLK high
  // - wait for DATA high in a blocking loop
  uint8_t data = numData>0 ? m_currentDevice->peek() : 0xFF;

  startParallelTransaction();

  // prepare data (bus is still in INPUT mode so the data will not be visible yet)
  // (doing it now saves time to meed the 50us timeout after DATA low)
  writeParallelData(data);

  noInterrupts();

  // signal "ready-to-send" (CLK=1)
  writePinCLK(HIGH);

  // wait for "ready-for-data" (DATA=0)
  JDEBUG1();
  if( !waitPinDATA(HIGH, 0) ) { atnRequest(); interrupts(); endParallelTransaction(); return false; }
  JDEBUG0();

  if( numData==0 ) 
    {
      // if we have nothing to send then there was some kind of error 
      // aborting here will signal the error condition to the receiver
      interrupts();
      endParallelTransaction();
      return false;
    }
  else if( numData==1 )
    {
      // last data byte => keep CLK high (signals EOI) and wait for receiver to 
      // confirm EOI by HIGH->LOW->HIGH pulse on DATA
      bool ok = (waitPinDATA(LOW) && waitPinDATA(HIGH));
      if( !ok ) { atnRequest(); interrupts(); endParallelTransaction(); return false; }
    }

  // output data on parallel bus
  JDEBUG1();
  setParallelBusModeOutput();
  JDEBUG0();

  // set CLK low (signal "data ready")
  writePinCLK(LOW);

  interrupts();
  endParallelTransaction();

  // discard data byte in device (read by peek() before)
  m_currentDevice->read();

  // remember initial bytes of data sent (see comment in transmitDolphinBurst)
  if( m_secondary==0x60 && m_dolphinCtr<DOLPHIN_PREBUFFER_BYTES ) 
    m_buffer[m_dolphinCtr++] = data;

  // wait for receiver to confirm receipt (must confirm within 1ms)
  bool res = waitPinDATA(LOW, 1000);
  
  // release parallel bus
  setParallelBusModeInput();
  
  return res;
}


bool IECBusHandler::receiveDolphinBurst()
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by pulling CLK low
  uint8_t n = 0;

  // clear any previous handshakes
  parallelBusHandshakeReceived();

  // pull DATA low
  writePinDATA(LOW);

  // confirm burst mode transmission
  parallelBusHandshakeTransmit();

  // keep going while CLK is low
  bool eoi = false;
  while( !eoi )
    {
      // wait for "data ready" handshake, return if ATN is asserted (high)
      if( !waitParallelBusHandshakeReceived() ) return false;

      // CLK=high means EOI ("final byte of data coming")
      eoi = readPinCLK();

      // get received data byte
      m_buffer[n++] = readParallelData();

      if( n<m_bufferSize && !eoi )
        {
          // data received and buffered  => send handshake
          parallelBusHandshakeTransmit();
        }
      else if( m_currentDevice->write(m_buffer, n, eoi)==n )
        {
          // data written successfully => send handshake
          parallelBusHandshakeTransmit();
          n = 0;
        }
      else
        {
          // error while writing data => release DATA to signal error condition and exit
          writePinDATA(HIGH);
          return false;
        }
    }

  return true;
}


bool IECBusHandler::transmitDolphinBurst()
{
  // NOTE: we only get here if sender has already signaled ready-to-receive
  // by pulling DATA low

  // send handshake to confirm burst transmission (Dolphin kernal EEDA)
  parallelBusHandshakeTransmit();

  // give the host some time to see our confirmation
  // if we send the next handshake too quickly then the host will see only one,
  // the host will be busy printing the load address after seeing the confirmation
  // so nothing is lost by waiting a good long time before the next handshake
  delayMicroseconds(1000);

  // switch parallel bus to output
  setParallelBusModeOutput();

  // when loading a file, DolphinDos switches to burst mode by sending "XQ" after
  // the transmission has started. The kernal does so after the first two bytes
  // were sent, MultiDubTwo after one byte. After swtiching to burst mode, the 1541
  // then re-transmits the bytes that were already sent.
  for(uint8_t i=0; i<m_dolphinCtr; i++)
    {
      // put data on bus
      writeParallelData(m_buffer[i]);

      // send handshake (see "send handshake" comment below)
      noInterrupts();
      parallelBusHandshakeTransmit();
      parallelBusHandshakeReceived();
      interrupts();

      // wait for received handshake
      if( !waitParallelBusHandshakeReceived() ) { setParallelBusModeInput(); return false; }
    }

  // get data from the device and transmit it
  uint8_t n;
  while( (n=m_currentDevice->read(m_buffer, m_bufferSize))>0 )
    {
      startParallelTransaction();
      for(uint8_t i=0; i<n; i++)
        {
          // put data on bus
          writeParallelData(m_buffer[i]);

          // send handshake
          // sending the handshake can induce a pulse on the receive handhake
          // line so we clear the receive handshake after sending, note that we
          // can't have an interrupt take up time between sending the handshake
          // and clearing the receive handshake
          noInterrupts();
          parallelBusHandshakeTransmit();
          parallelBusHandshakeReceived();
          interrupts();
          // wait for receiver handshake
          while( !parallelBusHandshakeReceived() )
            if( !readPinATN() || readPinDATA() )
              {
                // if receiver released DATA or pulled ATN low then there
                // was an error => release bus and CLK line and return
                setParallelBusModeInput();
                writePinCLK(HIGH);
                endParallelTransaction();
                return false;
              }
        }
      endParallelTransaction();
    }

  // switch parallel bus back to input
  setParallelBusModeInput();

  // after seeing our end-of-data and confirmit it, receiver waits for 2ms
  // for us to send the handshake (below) => no interrupts, otherwise we may
  // exceed the timeout
  noInterrupts();

  // signal end-of-data
  writePinCLK(HIGH);

  // wait for receiver to confirm
  if( !waitPinDATA(HIGH) ) { interrupts(); return false; }

  // send handshake
  parallelBusHandshakeTransmit();

  interrupts();

  return true;
}

#endif

#ifdef SUPPORT_EPYX

// ------------------------------------  Epyx FastLoad support routines  ------------------------------------


bool IECBusHandler::enableEpyxFastLoadSupport(IECDevice *dev, bool enable)
{
  if( enable && m_bufferSize>=32 )
    dev->m_sflags |= S_EPYX_ENABLED;
  else
    dev->m_sflags &= ~S_EPYX_ENABLED;

  // cancel any current requests
  dev->m_sflags &= ~(S_EPYX_HEADER|S_EPYX_LOAD|S_EPYX_SECTOROP);

  return (dev->m_sflags & S_EPYX_ENABLED)!=0;
}


void IECBusHandler::epyxLoadRequest(IECDevice *dev)
{
  if( dev->m_sflags & S_EPYX_ENABLED )
    dev->m_sflags |= S_EPYX_HEADER;
}


bool IECBusHandler::receiveEpyxByte(uint8_t &data)
{
  bool clk = HIGH;
  for(uint8_t i=0; i<8; i++)
    {
      // wait for next bit ready
      // can't use timeout because interrupts are disabled and (on some platforms) the
      // micros() function does not work in this case
      clk = !clk;
      if( !waitPinCLK(clk, 0) ) return false;

      // read next (inverted) bit
      JDEBUG1();
      data >>= 1;
      if( !readPinDATA() ) data |= 0x80;
      JDEBUG0();
    }

  return true;
}


bool IRAM_ATTR IECBusHandler::transmitEpyxByte(uint8_t data)
{
  // receiver expects all data bits to be inverted
  data = ~data;

  // prepare timer
  timer_init();
  timer_reset();

  // wait (indefinitely) for either DATA high ("ready-to-send") or ATN low
  // NOTE: this must be in a blocking loop since the sender starts transmitting
  // the byte immediately after setting CLK high. If we exit the "task" function then
  // we may not get back here in time to receive.
#ifdef ESP_PLATFORM
  while( !digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) )
    if( !timer_less_than(IWDT_FEED_TIME) )
      {
        // briefly enable interrupts to "feed" the WDT, otherwise we'll get re-booted
        interrupts(); noInterrupts();
        timer_reset();
      }
#else
  while( !digitalReadFastExt(m_pinDATA, m_regDATAread, m_bitDATA) && digitalReadFastExt(m_pinATN, m_regATNread, m_bitATN) );
#endif
  
  // start timer
  timer_start();
  JDEBUG1();

  // abort if ATN low
  if( !readPinATN() ) { JDEBUG0(); return false; }

  JDEBUG0();
  writePinCLK(data & bit(7));
  writePinDATA(data & bit(5));
  JDEBUG1();
  // bits 5+7 are read by receiver 15 cycles after DATA HIGH

  // wait until 17 us after DATA
  timer_wait_until(17);

  JDEBUG0();
  writePinCLK(data & bit(6));
  writePinDATA(data & bit(4));
  JDEBUG1();
  // bits 4+6 are read by receiver 25 cycles after DATA HIGH

  // wait until 27 us after DATA
  timer_wait_until(27);

  JDEBUG0();
  writePinCLK(data & bit(3));
  writePinDATA(data & bit(1));
  JDEBUG1();
  // bits 1+3 are read by receiver 35 cycles after DATA HIGH

  // wait until 37 us after DATA
  timer_wait_until(37);

  JDEBUG0();
  writePinCLK(data & bit(2));
  writePinDATA(data & bit(0));
  JDEBUG1();
  // bits 0+2 are read by receiver 45 cycles after DATA HIGH

  // wait until 47 us after DATA
  timer_wait_until(47);

  // release DATA and give it time to stabilize, also some
  // buffer time if we got slightly delayed when waiting before
  writePinDATA(HIGH);
  timer_wait_until(52);

  // wait for DATA low, receiver signaling "not ready"
  if( !waitPinDATA(LOW, 0) ) return false;

  JDEBUG0();
  return true;
}


#ifdef SUPPORT_EPYX_SECTOROPS

// NOTE: most calls to waitPinXXX() within this code happen while
// interrupts are disabled and therefore must use the ",0" (no timeout)
// form of the call - timeouts are dealt with using the micros() function
// which does not work properly when interrupts are disabled.

bool IECBusHandler::startEpyxSectorCommand(uint8_t command)
{
  // interrupts are assumed to be disabled when we get here
  // and will be re-enabled before we exit
  // both CLK and DATA must be released (HIGH) before entering
  uint8_t track, sector, data;

  if( command==0x81 )
    {
      // V1 sector write
      // wait for DATA low (no timeout), however we exit if ATN goes low,
      // interrupts are enabled while waiting (same as in 1541 code)
      interrupts();
      if( !waitPinDATA(LOW, 0) ) return false;
      noInterrupts();

      // release CLK
      writePinCLK(HIGH);
    }

  // receive track and sector
  // (command==1 means write sector, otherwise read sector)
  if( !receiveEpyxByte(track) )   { interrupts(); return false; }
  if( !receiveEpyxByte(sector) )  { interrupts(); return false; }

  // V1 of the cartridge has two different uploads for read and write
  // and therefore does not send the command separately
  if( command==0 && !receiveEpyxByte(command) ) { interrupts(); return false; }

  if( (command&0x7f)==1 )
    {
      // sector write operation => receive data
      for(int i=0; i<256; i++)
        if( !receiveEpyxByte(m_buffer[i]) )
          { interrupts(); return false; }
    }

  // pull CLK low to signal "not ready"
  writePinCLK(LOW);

  // we can allow interrupts again
  interrupts();

  // pass data on to the device
  if( (command&0x7f)==1 )
    if( !m_currentDevice->epyxWriteSector(track, sector, m_buffer) )
      { interrupts(); return false; }

  // m_buffer size is guaranteed to be >=32
  m_buffer[0] = command;
  m_buffer[1] = track;
  m_buffer[2] = sector;

  m_currentDevice->m_sflags |= S_EPYX_SECTOROP;
  return true;
}


bool IECBusHandler::finishEpyxSectorCommand()
{
  // this was set in receiveEpyxSectorCommand
  uint8_t command = m_buffer[0];
  uint8_t track   = m_buffer[1];
  uint8_t sector  = m_buffer[2];

  // receive data from the device
  if( (command&0x7f)!=1 )
    if( !m_currentDevice->epyxReadSector(track, sector, m_buffer) )
      return false;

  // all timing is clocked by the computer so we can't afford
  // interrupts to delay execution as long as we are signaling "ready"
  noInterrupts();

  // release CLK to signal "ready"
  writePinCLK(HIGH);

  if( command==0x81 )
    {
      // V1 sector write => receive new track/sector
      return startEpyxSectorCommand(0x81); // startEpyxSectorCommand() re-enables interrupts
    }
  else
    {
      // V1 sector read or V2/V3 read/write => release CLK to signal "ready"
      if( (command&0x7f)!=1 )
        {
          // sector read operation => send data
          for(int i=0; i<256; i++)
            if( !transmitEpyxByte(m_buffer[i]) )
              { interrupts(); return false; }
        }
      else
        {
          // release DATA and wait for computer to pull it LOW
          writePinDATA(HIGH);
          if( !waitPinDATA(LOW, 0) ) { interrupts(); return false; }
        }

      // release DATA and toggle CLK until DATA goes high or ATN goes low.
      // This provides a "heartbeat" for the computer so it knows we're still running
      // the EPYX sector command code. If the computer does not see this heartbeat
      // it will re-upload the code when it needs it.
      // The EPYX code running on a real 1541 drive does not have this timeout but
      // we need it because otherwise we're stuck in an endless loop with interrupts
      // disabled until the computer either pulls ATN low or releases DATA
      // We can not enable interrupts because the time between DATA high
      // and the start of transmission for the next track/sector/command block
      // is <400us without any chance for us to signal "not ready.
      // A (not very nice) interrupt routing may take longer than that.
      // We could just always quit and never send the heartbeat but then operations
      // like "copy disk" would have to re-upload the code for ever single sector.
      // Wait for DATA high, time out after 30000 * ~16us (~500ms)
      timer_init();
      timer_reset();
      timer_start();
      for(unsigned int i=0; i<30000; i++)
        {
          writePinCLK(LOW);
          if( !readPinATN() ) break;
          interrupts();
          timer_wait_until(8);
          noInterrupts();
          writePinCLK(HIGH);
          if( readPinDATA() ) break;
          timer_wait_until(16);
          timer_reset();
        }

      // abort if we timed out (DATA still low) or ATN is pulled
      if( !readPinDATA() || !readPinATN() ) { interrupts(); return false; }

      // wait (DATA high pulse from sender can be up to 90us)
      if( !waitTimeout(100) ) { interrupts(); return false; }

      // if DATA is still high (or ATN is low) then done, otherwise repeat for another sector
      if( readPinDATA() || !readPinATN() )
        { interrupts(); return false; }
      else
        return startEpyxSectorCommand((command&0x80) ? command : 0); // startEpyxSectorCommand() re-enables interrupts
    }
}

#endif

bool IECBusHandler::receiveEpyxHeader()
{
  // all timing is clocked by the computer so we can't afford
  // interrupts to delay execution as long as we are signaling "ready"
  noInterrupts();

  // pull CLK low to signal "ready for header"
  writePinCLK(LOW);

  // wait for sender to set DATA low, signaling "ready"
  if( !waitPinDATA(LOW, 0) ) { interrupts(); return false; }

  // release CLK line
  writePinCLK(HIGH);

  // receive fastload routine upload (256 bytes) and compute checksum
  uint8_t data, checksum = 0;
  for(int i=0; i<256; i++)
    {
      if( !receiveEpyxByte(data) ) { interrupts(); return false; }
      checksum += data;
    }

  if( checksum==0x26 /* V1 load file */ ||
      checksum==0x86 /* V2 load file */ ||
      checksum==0xAA /* V3 load file */ )
    {
      // LOAD FILE operation
      // receive file name and open file
      uint8_t n;
      if( receiveEpyxByte(n) && n>0 && n<=32 )
        {
          // file name arrives in reverse order
          for(uint8_t i=n; i>0; i--)
            if( !receiveEpyxByte(m_buffer[i-1]) )
              { interrupts(); return false; }

          // pull CLK low to signal "not ready"
          writePinCLK(LOW);

          // can allow interrupts again
          interrupts();

          // initiate DOS OPEN command in the device (open channel #0)
          m_currentDevice->listen(0xF0);

          // send file name (in proper order) to the device
          for(uint8_t i=0; i<n; i++)
            {
              // make sure the device can accept data
              int8_t ok;
              while( (ok = m_currentDevice->canWrite())<0 )
                if( !readPinATN() )
                  return false;

              // fail if it can not
              if( ok==0 ) return false;

              // send next file name character
              m_currentDevice->write(m_buffer[i], i<n-1);
            }

          // finish DOS OPEN command in the device
          m_currentDevice->unlisten();

          m_currentDevice->m_sflags |= S_EPYX_LOAD;
          return true;
        }
    }
#ifdef SUPPORT_EPYX_SECTOROPS
  else if( checksum==0x0B /* V1 sector read */ )
    return startEpyxSectorCommand(0x82); // startEpyxSectorCommand re-enables interrupts
  else if( checksum==0xBA /* V1 sector write */ )
    return startEpyxSectorCommand(0x81); // startEpyxSectorCommand re-enables interrupts
  else if( checksum==0xB8 /* V2 and V3 sector read or write */ )
    return startEpyxSectorCommand(0); // startEpyxSectorCommand re-enables interrupts
#endif
#if 0
  else if( Serial )
    {
      interrupts();
      Serial.print(F("Unknown EPYX fastload routine, checksum is 0x"));
      Serial.println(checksum, HEX);
    }
#endif

  interrupts();
  return false;
}


bool IECBusHandler::transmitEpyxBlock()
{
  // set channel number for read() call below
  m_currentDevice->talk(0);

  // get data
  m_inTask = false;
  uint8_t n = m_currentDevice->read(m_buffer, m_bufferSize);
  m_inTask = true;
  if( (m_flags & P_ATN) || !readPinATN() ) return false;

  noInterrupts();

  // release CLK to signal "ready"
  writePinCLK(HIGH);

  // transmit length of this data block
  if( !transmitEpyxByte(n) ) { interrupts(); return false; }

  // transmit the data block
  for(uint8_t i=0; i<n; i++)
    if( !transmitEpyxByte(m_buffer[i]) )
      { interrupts(); return false; }

  // pull CLK low to signal "not ready"
  writePinCLK(LOW);

  interrupts();

  // the "end transmission" condition for the receiver is receiving
  // a "0" length byte so we keep sending block until we have
  // transmitted a 0-length block (i.e. end-of-file)
  return n>0;
}


#endif

// ------------------------------------  IEC protocol support routines  ------------------------------------  


bool IECBusHandler::receiveIECByteATN(uint8_t &data)
{
  // wait for CLK=1
  if( !waitPinCLK(HIGH, 0) ) return false;

  // release DATA ("ready-for-data")
  writePinDATA(HIGH);

  // wait for CLK=0
  // must wait indefinitely since other devices may be holding DATA low until
  // they are ready, bus master will start sending as soon as all devices have
  // released DATA
  if( !waitPinCLK(LOW, 0) ) return false;

  // receive data bits
  data = 0;
  for(uint8_t i=0; i<8; i++)
    {
      // wait for CLK=1, signaling data is ready
      JDEBUG1();

#ifdef SUPPORT_JIFFY
      // JiffyDos protocol detection
      if( (i==7) && (&data==&m_primary) && !waitPinCLK(HIGH, 200) )
        {
          IECDevice *dev = findDevice((data>>1)&0x1F);
          JDEBUG0();
          if( (dev!=NULL) && (dev->m_sflags&S_JIFFY_ENABLED) )
            {
              JDEBUG1();
              // when sending final bit of primary address byte under ATN, host
              // delayed CLK=1 by more than 200us => JiffyDOS protocol detection
              // => if JiffyDOS support is enabled and we are being addressed then
              // respond that we support the protocol by pulling DATA low for 80us
              dev->m_sflags |= S_JIFFY_DETECTED;
              writePinDATA(LOW);
              if( !waitTimeout(80) ) return false;
              writePinDATA(HIGH);
            }
        }
#endif

      if( !waitPinCLK(HIGH) ) return false;
      JDEBUG0();

      // read DATA bit
      data >>= 1;
      if( readPinDATA() ) data |= 0x80;

      // wait for CLK=0, signaling "data not ready"
      if( !waitPinCLK(LOW) ) return false;
    }

  // Acknowledge receipt by pulling DATA low
  writePinDATA(LOW);

#if defined(SUPPORT_DOLPHIN)
  // DolphinDos parallel cable detection:
  // after receiving secondary address, wait for either:
  //  HIGH->LOW edge (1us pulse) on incoming parallel handshake signal, 
  //      if received pull outgoing parallel handshake signal LOW to confirm
  //  LOW->HIGH edge on ATN, 
  //      if so then timeout, host does not support DolphinDos

  IECDevice *dev = findDevice(m_primary & 0x1F);
  if( dev!=NULL && (dev->m_sflags & S_DOLPHIN_ENABLED) && (&data==&m_secondary) )
    if( parallelCableDetect() )
      {
        dev->m_sflags |= S_DOLPHIN_DETECTED;
        parallelBusHandshakeTransmit();
      }
#endif

  return true;
}


bool IECBusHandler::receiveIECByte(bool canWriteOk)
{
  // NOTE: we only get here if sender has already signaled ready-to-send
  // by releasing CLK
  bool eoi = false;

  noInterrupts();

  // release DATA ("ready-for-data")
  writePinDATA(HIGH);

  // wait for sender to set CLK=0 ("ready-to-send")
  if( !waitPinCLK(LOW, 200) )
    {
      // exit if waitPinCLK returned because of falling edge on ATN
      if( !readPinATN() ) { interrupts(); return false; }

      // sender did not set CLK=0 within 200us after we set DATA=1
      // => it is signaling EOI (not so if we are under ATN)
      // acknowledge we received it by setting DATA=0 for 80us
      eoi = true;
      writePinDATA(LOW);
      if( !waitTimeout(80) ) { interrupts(); return false; }
      writePinDATA(HIGH);

      // keep waiting for CLK=0
      if( !waitPinCLK(LOW) ) { interrupts(); return false; }
    }

  // receive data bits
  uint8_t data = 0;
  for(uint8_t i=0; i<8; i++)
    {
      // wait for CLK=1, signaling data is ready
      if( !waitPinCLK(HIGH) ) { interrupts(); return false; }

      // read DATA bit
      data >>= 1;
      if( readPinDATA() ) data |= 0x80;

      // wait for CLK=0, signaling "data not ready"
      if( !waitPinCLK(LOW) ) { interrupts(); return false; }
    }

  interrupts();

  if( canWriteOk )
    {
      // acknowledge receipt by pulling DATA low
      writePinDATA(LOW);

      // pass received data on to the device
      m_currentDevice->write(data, eoi);
      return true;
    }
  else
    {
      // canWrite() reported an error
      return false;
    }
}


bool IECBusHandler::transmitIECByte(uint8_t numData)
{
  // check whether ready-to-receive was already signaled by the 
  // receiver before we signaled ready-to-send. The 1541 ROM 
  // disassembly (E919-E924) suggests that this signals a "verify error" 
  // condition and we should send EOI. Note that the C64 kernal does not
  // actually do this signaling during a "verify" operation so I don't
  // know whether my interpretation here is correct. However, some 
  // programs (e.g. "copy 190") lock up if we don't handle this case.
  bool verifyError = readPinDATA();

  noInterrupts();

  // signal "ready-to-send" (CLK=1)
  writePinCLK(HIGH);
  
  // wait (indefinitely, no timeout) for DATA HIGH ("ready-to-receive")
  // NOTE: this must be in a blocking loop since the receiver starts counting
  // up the EOI timeout immediately after setting DATA HIGH. If we had exited the 
  // "task" function then it might be more than 200us until we get back here
  // to pull CLK low and the receiver will interpret that delay as EOI.
  if( !waitPinDATA(HIGH, 0) ) { interrupts(); return false; }
  
  if( numData==1 || verifyError )
    {
      // only this byte left to send => signal EOI by keeping CLK=1
      // wait for receiver to acknowledge EOI by setting DATA=0 then DATA=1
      // if we got here by "verifyError" then wait indefinitely because we
      // didn't enter the "wait for DATA high" state above
      if( !waitPinDATA(LOW, verifyError ? 0 : 1000) ) { interrupts(); return false; }
      if( !waitPinDATA(HIGH) ) { interrupts(); return false; }
    }

  // if we have nothing to send then there was some kind of error 
  // => aborting at this stage will signal the error condition to the receiver
  //    (e.g. "File not found" for LOAD)
  if( numData==0 ) { interrupts(); return false; }

  // signal "data not valid" (CLK=0)
  writePinCLK(LOW);

  interrupts();

  // get the data byte from the device
  uint8_t data = m_currentDevice->read();

  // transmit the byte
  for(uint8_t i=0; i<8; i++)
    {
      // signal "data not valid" (CLK=0)
      writePinCLK(LOW);

      // set bit on DATA line
      writePinDATA((data & 1)!=0);

      // hold for 80us
      if( !waitTimeout(80) ) return false;
      
      // signal "data valid" (CLK=1)
      writePinCLK(HIGH);

      // hold for 60us
      if( !waitTimeout(60) ) return false;

      // next bit
      data >>= 1;
    }

  // pull CLK=0 and release DATA=1 to signal "busy"
  writePinCLK(LOW);
  writePinDATA(HIGH);

  // wait for receiver to signal "busy"
  if( !waitPinDATA(LOW) ) return false;
  
  return true;
}


// called when a falling edge on ATN is detected, either by the pin change
// interrupt handler or by polling within the microTask function
void IRAM_ATTR IECBusHandler::atnRequest()
{
  // check if ATN is actually LOW, if not then just return (stray interrupt request)
  if( readPinATN() ) return;

  // falling edge on ATN detected (bus master addressing all devices)
  m_flags |= P_ATN;
  m_flags &= ~P_DONE;
  m_currentDevice = NULL;

  // ignore anything for 100us after ATN falling edge
#ifdef ESP_PLATFORM
  // calling "micros()" (aka esp_timer_get_time()) within an interrupt handler
  // on ESP32 appears to sometimes return incorrect values. This was observed
  // when running Meatloaf on a LOLIN D32 board. So we just note that the 
  // timeout needs to be started and will actually set m_timeoutStart outside 
  // of the interrupt handler within the task() function
  m_timeoutStart = 0xFFFFFFFF;
#else
  m_timeoutStart = micros();
#endif

  // release CLK (in case we were holding it LOW before)
  writePinCLK(HIGH);
  
  // set DATA=0 ("I am here").  If nobody on the bus does this within 1ms,
  // busmaster will assume that "Device not present" 
  writePinDATA(LOW);

  // disable the hardware that allows ATN to pull DATA low
  writePinCTRL(HIGH);

#ifdef SUPPORT_JIFFY
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->m_sflags &= ~(S_JIFFY_DETECTED|S_JIFFY_BLOCK);
#endif
#ifdef SUPPORT_DOLPHIN
  for(uint8_t i=0; i<m_numDevices; i++) 
    m_devices[i]->m_sflags &= ~(S_DOLPHIN_BURST_TRANSMIT|S_DOLPHIN_BURST_RECEIVE|S_DOLPHIN_DETECTED);
#endif
#ifdef SUPPORT_EPYX
  for(uint8_t i=0; i<m_numDevices; i++) 
    m_devices[i]->m_sflags &= ~(S_EPYX_HEADER|S_EPYX_LOAD|S_EPYX_SECTOROP);
#endif
}


void IECBusHandler::task()
{
  // don't do anything if begin() hasn't been called yet
  if( m_flags==0xFF ) return;

  // prevent interrupt handler from calling atnRequest()
  m_inTask = true;

  // ------------------ check for activity on RESET pin -------------------

  if( readPinRESET() )
    m_flags |= P_RESET;
  else if( (m_flags & P_RESET)!=0 )
    { 
      // falling edge on RESET pin
      m_flags = 0;
      
      // release CLK and DATA, allow ATN to pull DATA low in hardware
      writePinCLK(HIGH);
      writePinDATA(HIGH);
      writePinCTRL(LOW);

      // call "reset" function for attached devices
      for(uint8_t i=0; i<m_numDevices; i++)
        m_devices[i]->reset(); 
    }

  // ------------------ check for activity on ATN pin -------------------

  if( !(m_flags & P_ATN) && !readPinATN() )
    {
      // falling edge on ATN (bus master addressing all devices)
      atnRequest();
    } 

#ifdef ESP_PLATFORM
  // see comment in atnRequest function
  if( (m_flags & P_ATN)!=0 && !readPinATN() &&
      (m_timeoutStart==0xFFFFFFFF ? (m_timeoutStart=micros(),false) : (micros()-m_timeoutStart)>100) &&
      readPinCLK() )
#else
  if( (m_flags & P_ATN)!=0 && !readPinATN() && (micros()-m_timeoutStart)>100 && readPinCLK() )
#endif
    {
      // we are under ATN, have waited 100us and the host has released CLK
      // => no more interrupts until the ATN sequence is finished. If we allowed interrupts
      //    and a long interrupt occurred close to the end of the sequence then we may miss
      //    a quick ATN low->high->low sequence, i.e completely missing the start of a new
      //    ATN request.
      noInterrupts();

      // P_DONE flag may have gotten set again after it was reset in atnRequest()
      m_flags &= ~P_DONE;

      m_primary = 0;
      if( receiveIECByteATN(m_primary) && ((m_primary == 0x3f) || (m_primary == 0x5f) || (findDevice((unsigned int) m_primary & 0x1f)!=NULL)) )
        {
          // this is either UNLISTEN or UNTALK or we were addressed
          // => receive the secondary address, assume 0 if not sent
          if( (m_primary == 0x3f) || (m_primary == 0x5f) || !receiveIECByteATN(m_secondary) ) m_secondary = 0;

          // wait until ATN is released
          waitPinATN(HIGH);
          m_flags &= ~P_ATN;

          // allow ATN to pull DATA low in hardware
          writePinCTRL(LOW);
          
          if( (m_primary & 0xE0)==0x20 && (m_currentDevice = findDevice(m_primary & 0x1F))!=NULL )
            {
              // we were told to listen
              m_currentDevice->listen(m_secondary);
              m_flags &= ~P_TALKING;
              m_flags |= P_LISTENING;
#ifdef SUPPORT_DOLPHIN
              // see comments in function receiveDolphinByte
              if( m_secondary==0x61 ) m_dolphinCtr = 2*DOLPHIN_PREBUFFER_BYTES;
#endif
              // set DATA=0 ("I am here")
              writePinDATA(LOW);
            }
          else if( (m_primary & 0xE0)==0x40 && (m_currentDevice = findDevice(m_primary & 0x1F))!=NULL )
            {
              // we were told to talk
#ifdef SUPPORT_JIFFY
              if( (m_currentDevice->m_sflags & S_JIFFY_DETECTED)!=0 && m_secondary==0x61 )
                { 
                  // in JiffyDOS, secondary 0x61 when talking enables "block transfer" mode
                  m_secondary = 0x60; 
                  m_currentDevice->m_sflags |= S_JIFFY_BLOCK; 
                }
#endif        
              m_currentDevice->talk(m_secondary);
              m_flags &= ~P_LISTENING;
              m_flags |= P_TALKING;
#ifdef SUPPORT_DOLPHIN
              // see comments in function transmitDolphinByte
              if( m_secondary==0x60 ) m_dolphinCtr = 0;
#endif
              // wait for bus master to set CLK=1 (and DATA=0) for role reversal
              if( waitPinCLK(HIGH) )
                {
                  // now set CLK=0 and DATA=1
                  writePinCLK(LOW);
                  writePinDATA(HIGH);
                  
                  // wait 80us before transmitting first byte of data
                  delayMicrosecondsISafe(80);
                  m_timeoutDuration = 0;
                }
            }
          else if( (m_primary == 0x3f) && (m_flags & P_LISTENING) )
            {
              // all devices were told to stop listening
              m_flags &= ~P_LISTENING;
              for(uint8_t i=0; i<m_numDevices; i++)
                m_devices[i]->unlisten();
            }
          else if( m_primary == 0x5f && (m_flags & P_TALKING) )
            {
              // all devices were told to stop talking
              m_flags &= ~P_TALKING;
              for(uint8_t i=0; i<m_numDevices; i++)
                m_devices[i]->untalk();
            }
          
          if( !(m_flags & (P_LISTENING | P_TALKING)) )
            {
              // we're neither listening nor talking => release CLK/DATA
              writePinCLK(HIGH);
              writePinDATA(HIGH);
            }
        }
      else
        {
          // either we were not addressed or there was an error receiving the primary address
          delayMicrosecondsISafe(150);
          writePinCLK(HIGH);
          writePinDATA(HIGH);
          waitPinATN(HIGH);

          // if someone else was told to start talking then we must stop
          if( (m_primary & 0xE0)==0x40 ) m_flags &= ~P_TALKING;

          // allow ATN to pull DATA low in hardware
          writePinCTRL(LOW);
        }

      interrupts();

      if( (m_flags & P_LISTENING)!=0 )
        {
          // a device is supposed to listen, check if it can accept data
          // (meanwhile allow atnRequest to be called in interrupt)
          IECDevice *dev = m_currentDevice;
          m_inTask = false;
          dev->task();
          bool canWrite = (dev->canWrite()>0);
          m_inTask = true;

          // m_currentDevice could have been reset to NULL while m_inTask was 'false'
          if( m_currentDevice!=NULL && !canWrite )
            {
              // device can't accept data => signal error by releasing DATA line
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
        }
    }
  else if( (m_flags & P_ATN)!=0 && readPinATN() )
    {
      // host has released ATN
      m_flags &= ~P_ATN;
    }

#ifdef SUPPORT_DOLPHIN
  // ------------------ DolphinDos burst transfer handling -------------------

  for(uint8_t devidx=0; devidx<m_numDevices; devidx++)
  if( (m_devices[devidx]->m_sflags & S_DOLPHIN_BURST_TRANSMIT)!=0 && (micros()-m_timeoutStart)>200 && !readPinDATA() )
    {
      // if we are in burst transmit mode, give other devices 200us to release
      // the DATA line and wait for the host to pull DATA LOW

      // pull CLK line LOW (host should have released it by now)
      writePinCLK(LOW);
      
      m_currentDevice = m_devices[devidx];
      if( m_currentDevice->m_sflags & S_DOLPHIN_BURST_ENABLED )
        {
          // transmit data in burst mode
          transmitDolphinBurst();
          
          // close the file (usually the host sends these but not in burst mode)
          m_currentDevice->listen(0xE0);
          m_currentDevice->unlisten();

          // check whether ATN has been asserted and handle if necessary
          if( !readPinATN() ) atnRequest();
        }
      else
        {
          // switch to regular transmit mode
          m_flags = P_TALKING;
          m_currentDevice->m_sflags |= S_DOLPHIN_DETECTED;
          m_secondary = 0x60;
        }

      if( m_currentDevice!=NULL ) m_currentDevice->m_sflags &= ~S_DOLPHIN_BURST_TRANSMIT;
    }
  else if( (m_devices[devidx]->m_sflags&S_DOLPHIN_BURST_RECEIVE)!=0 && (micros()-m_timeoutStart)>500 && !readPinCLK() )
    {
      // if we are in burst receive mode, wait 500us to make sure host has released CLK after 
      // sending "XZ" burst request (Dolphin kernal ef82), and wait for it to pull CLK low again
      // (if we don't wait at first then we may read CLK=0 already before the host has released it)

      m_currentDevice = m_devices[devidx];
      if( m_currentDevice->m_sflags & S_DOLPHIN_BURST_ENABLED )
        {
          // transmit data in burst mode
          receiveDolphinBurst();
          
          // check whether ATN has been asserted and handle if necessary
          if( !readPinATN() ) atnRequest();
        }
      else
        {
          // switch to regular receive mode
          m_flags = P_LISTENING;
          m_currentDevice->m_sflags |= S_DOLPHIN_DETECTED;
          m_secondary = 0x61;

          // see comment in function receiveDolphinByte
          m_dolphinCtr = (2*DOLPHIN_PREBUFFER_BYTES)-m_dolphinCtr;

          // signal NOT ready to receive
          writePinDATA(LOW);
        }

      if( m_currentDevice!=NULL ) m_currentDevice->m_sflags &= ~S_DOLPHIN_BURST_RECEIVE;
    }
#endif

#ifdef SUPPORT_EPYX
  // ------------------ Epyx FastLoad transfer handling -------------------

  for(uint8_t devidx=0; devidx<m_numDevices; devidx++)
  if( (m_devices[devidx]->m_sflags & S_EPYX_HEADER) && readPinDATA() )
    {
      m_currentDevice = m_devices[devidx];
      m_currentDevice->m_sflags &= ~S_EPYX_HEADER;
      if( !receiveEpyxHeader() )
        {
          // transmission error
          writePinCLK(HIGH);
          writePinDATA(HIGH);
        }
    }
  else if( m_devices[devidx]->m_sflags & S_EPYX_LOAD )
    {
      m_currentDevice = m_devices[devidx];
      if( !transmitEpyxBlock() )
        {
          // either end-of-data or transmission error => we are done
          writePinCLK(HIGH);
          writePinDATA(HIGH);

          // close the file (was opened in receiveEpyxHeader)
          m_currentDevice->listen(0xE0);
          m_currentDevice->unlisten();

          // no more data to send
          m_currentDevice->m_sflags &= ~S_EPYX_LOAD;
        }
    }
#ifdef SUPPORT_EPYX_SECTOROPS
  else if( m_devices[devidx]->m_sflags & S_EPYX_SECTOROP )
    {
      m_currentDevice = m_devices[devidx];
      if( !finishEpyxSectorCommand() )
        {
          // either no more operations or transmission error => we are done
          writePinCLK(HIGH);
          writePinDATA(HIGH);

          // no more sector operations
          m_currentDevice->m_sflags &= ~S_EPYX_SECTOROP;
        }
    }
#endif
#endif

  // ------------------ receiving data -------------------

  if( (m_flags & (P_ATN|P_LISTENING|P_DONE))==P_LISTENING && (m_currentDevice!=NULL) )
    {
     // we are not under ATN, are in "listening" mode and not done with the transaction

      // check if we can write (also gives devices a chance to
      // execute time-consuming tasks while bus master waits for ready-for-data)
      IECDevice *dev = m_currentDevice;
      m_inTask = false;
      int8_t numData = dev->canWrite();
      m_inTask = true;

      if( m_currentDevice==NULL )
        { /* m_currentDevice was reset while we were stuck in "canRead" */ }
      else if( !readPinATN() )
        {
          // a falling edge on ATN happened while we were stuck in "canWrite"
          atnRequest();
        }
#ifdef SUPPORT_JIFFY
      else if( (m_currentDevice->m_sflags & S_JIFFY_DETECTED)!=0 && numData>=0 )
        {
          // receiving under JiffyDOS protocol
          if( !receiveJiffyByte(numData>0) )
            {
              // receive failed => release DATA 
              // and stop listening.  This will signal
              // an error condition to the sender
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
          }
#endif
#ifdef SUPPORT_DOLPHIN
      else if( (m_currentDevice->m_sflags & S_DOLPHIN_DETECTED)!=0 && numData>=0 )
        {
          // receiving under DolphinDOS protocol
          if( !readPinCLK() )
            { /* CLK is still low => sender is not ready yet */ }
          else if( !receiveDolphinByte(numData>0) )
            {
              // receive failed => release DATA 
              // and stop listening.  This will signal
              // an error condition to the sender
              writePinDATA(HIGH);
              m_flags |= P_DONE;
            }
        }
#endif
      else if( numData>=0 && readPinCLK() )
        {
          // either under ATN (in which case we always accept data)
          // or canWrite() result was non-negative
          // CLK high signals sender is ready to transmit
          if( !receiveIECByte(numData>0) )
            {
              // receive failed => transaction is done
              m_flags |= P_DONE;
            }
        }
    }

  // ------------------ transmitting data -------------------

  if( (m_flags & (P_ATN|P_TALKING|P_DONE))==P_TALKING && (m_currentDevice!=NULL) )
   {
     // we are not under ATN, are in "talking" mode and not done with the transaction

#ifdef SUPPORT_JIFFY
     if( (m_currentDevice->m_sflags & S_JIFFY_BLOCK)!=0 )
       {
         // JiffyDOS block transfer mode
         m_inTask = false;
         uint8_t numData = m_currentDevice->read(m_buffer, m_bufferSize);
         m_inTask = true;

         // delay to make sure receiver sees our CLK LOW and enters "new data block" state.
         // If a possible VIC "bad line" occurs right after reading bits 6+7 it may take
         // the receiver up to 160us after reading bits 6+7 (at FB71) to checking for CLK low (at FB54).
         // If we make it back into transmitJiffyBlock() during that time period
         // then we may already set CLK HIGH again before receiver sees the CLK LOW, 
         // preventing the receiver from going into "new data block" state
         while( (micros()-m_timeoutStart)<175 );

         if( (m_flags & P_ATN) || !readPinATN() || !transmitJiffyBlock(m_buffer, numData) )
           {
             // either a transmission error, no more data to send or falling edge on ATN
             m_flags |= P_DONE;
           }
         else
           {
             // remember time when previous transmission finished
             m_timeoutStart = micros();
           }
       }
     else
#endif
       {
        // check if we can read (also gives devices a chance to
        // execute time-consuming tasks while bus master waits for ready-to-send)
        IECDevice *dev = m_currentDevice;
        m_inTask = false;
        int8_t numData = dev->canRead();
        m_inTask = true;

        if( m_currentDevice==NULL )
          { /* m_currentDevice was reset while we were stuck in "canRead" */ }
        else if( !readPinATN() )
          {
            // a falling edge on ATN happened while we were stuck in "canRead"
            atnRequest();
          }
        else if( (micros()-m_timeoutStart)<m_timeoutDuration || numData<0 )
          {
            // either timeout not yet met or canRead() returned a negative value => do nothing
          }
#ifdef SUPPORT_JIFFY
        else if( (m_currentDevice->m_sflags & S_JIFFY_DETECTED)!=0 )
          {
            // JiffyDOS byte-by-byte transfer mode
            if( !transmitJiffyByte(numData) )
              {
                // either a transmission error, no more data to send or falling edge on ATN
                m_flags |= P_DONE;
              }
          }
#endif
#ifdef SUPPORT_DOLPHIN
        else if( (m_currentDevice->m_sflags & S_DOLPHIN_DETECTED)!=0 )
          {
            // DolphinDOS byte-by-byte transfer mode
            if( !transmitDolphinByte(numData) )
              {
                // either a transmission error, no more data to send or falling edge on ATN
                writePinCLK(HIGH);
                m_flags |= P_DONE;
              }
          }
#endif
        else
          {
            // regular IEC transfer
            if( transmitIECByte(numData) )
              {
                // delay before next transmission ("between bytes time")
                m_timeoutStart = micros();
                m_timeoutDuration = 200;
              }
            else
              {
                // either a transmission error, no more data to send or falling edge on ATN
                m_flags |= P_DONE;
              }
          }
       }
   }

  // allow the interrupt handler to call atnRequest() again
  m_inTask = false;

  // if ATN is low and we don't have P_ATN then we missed the falling edge,
  // make sure to process it before we leave
  if( m_atnInterrupt!=NOT_AN_INTERRUPT && !readPinATN() && !(m_flags & P_ATN) ) { noInterrupts(); atnRequest(); interrupts(); }

  // call "task" function for attached devices
  for(uint8_t i=0; i<m_numDevices; i++)
    m_devices[i]->task(); 
}
