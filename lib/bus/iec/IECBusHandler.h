// -----------------------------------------------------------------------------
// Copyright (C) 2023 David Hansel
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
// You should have receikved a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#ifndef IECBUSHANDLER_H
#define IECBUSHANDLER_H

#include "IECConfig.h"
#include <stdint.h>

#if defined(__AVR__)
#define IOREG_TYPE uint8_t
#elif defined(ARDUINO_UNOR4_MINIMA) || defined(ARDUINO_UNOR4_WIFI)
#define IOREG_TYPE uint16_t
#elif defined(__SAM3X8E__) || defined(ESP_PLATFORM)
#define IOREG_TYPE uint32_t
#endif

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#define INTERRUPT_FCN_ARG void *
#else
#define INTERRUPT_FCN_ARG
#endif

class IECDevice;

class IECBusHandler
{
 public:
  // pinATN should preferrably be a pin that can handle external interrupts
  // (e.g. 2 or 3 on the Arduino UNO), if not then make sure the task() function
  // gets called at least once evey millisecond, otherwise "device not present" 
  // errors may result
  IECBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinDATA, uint8_t pinRESET = 0xFF, uint8_t pinCTRL = 0xFF, uint8_t pinSRQ = 0xFF);

  // must be called once at startup before the first call to "task", devnr
  // is the IEC bus device number that this device should react to
  void begin();

  bool attachDevice(IECDevice *dev);
  bool detachDevice(IECDevice *dev);

  // task must be called periodically to handle IEC bus communication
  // if the ATN signal is NOT on an interrupt-capable pin then task() must be
  // called at least once every millisecond, otherwise less frequent calls are
  // ok but bus communication will be slower if called less frequently.
  void task();

#if (defined(SUPPORT_JIFFY) || defined(SUPPORT_DOLPHIN) || defined(SUPPORT_EPYX)) && !defined(IEC_DEFAULT_FASTLOAD_BUFFER_SIZE)
  // if IEC_DEFAULT_FASTLOAD_BUFFER_SIZE is set to 0 then the buffer space used
  // by fastload protocols can be set dynamically using the setBuffer function.
  void setBuffer(uint8_t *buffer, uint8_t bufferSize);
#endif

#ifdef SUPPORT_JIFFY 
  bool enableJiffyDosSupport(IECDevice *dev, bool enable);
#endif

#ifdef SUPPORT_EPYX
  bool enableEpyxFastLoadSupport(IECDevice *dev, bool enable);
  void epyxLoadRequest(IECDevice *dev);
#endif

#ifdef SUPPORT_DOLPHIN
  // call this BEFORE begin() if you do not want to use the default pins for the DolphinDos cable
#ifdef SUPPORT_DOLPHIN_XRA1405
  void setDolphinDosPins(uint8_t pinHT, uint8_t pinHR, uint8_t pinSCK, uint8_t pinCOPI, uint8_t pinCIPO, uint8_t pinCS);
#else
  void setDolphinDosPins(uint8_t pinHT, uint8_t pinHR, uint8_t pinD0, uint8_t pinD1, uint8_t pinD2, uint8_t pinD3, 
                         uint8_t pinD4, uint8_t pinD5, uint8_t pinD6, uint8_t pinD7);
#endif

  bool enableDolphinDosSupport(IECDevice *dev, bool enable);
  void enableDolphinBurstMode(IECDevice *dev, bool enable);
  void dolphinBurstReceiveRequest(IECDevice *dev);
  void dolphinBurstTransmitRequest(IECDevice *dev);
#endif

  IECDevice *findDevice(uint8_t devnr, bool includeInactive = false);
  bool canServeATN();
  bool inTransaction();
  void sendSRQ();

  IECDevice *m_currentDevice;
  IECDevice *m_devices[MAX_DEVICES];

  uint8_t m_numDevices;
  int  m_atnInterrupt;
  uint8_t m_pinATN, m_pinCLK, m_pinDATA, m_pinRESET, m_pinSRQ, m_pinCTRL;

 private:
  inline bool readPinATN();
  inline bool readPinCLK();
  inline bool readPinDATA();
  inline bool readPinRESET();
  inline void writePinCLK(bool v);
  inline void writePinDATA(bool v);
  void writePinCTRL(bool v);
  bool waitTimeout(uint16_t timeout, uint8_t cond = 0);
  bool waitPinDATA(bool state, uint16_t timeout = 1000);
  bool waitPinCLK(bool state, uint16_t timeout = 1000);
  void waitPinATN(bool state);
  void atnRequest();
  bool receiveIECByteATN(uint8_t &data);
  bool receiveIECByte(bool canWriteOk);
  bool transmitIECByte(uint8_t numData);

  volatile uint16_t m_timeoutDuration; 
  volatile uint32_t m_timeoutStart;
  volatile bool m_inTask;
  volatile uint8_t m_flags;
  uint8_t m_primary, m_secondary;

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regCLKwrite, *m_regCLKmode, *m_regDATAwrite, *m_regDATAmode;
  volatile const IOREG_TYPE *m_regATNread, *m_regCLKread, *m_regDATAread, *m_regRESETread;
  IOREG_TYPE m_bitATN, m_bitCLK, m_bitDATA, m_bitRESET;
#endif

#ifdef SUPPORT_JIFFY 
  bool receiveJiffyByte(bool canWriteOk);
  bool transmitJiffyByte(uint8_t numData);
  bool transmitJiffyBlock(uint8_t *buffer, uint8_t numBytes);
#endif

#ifdef SUPPORT_DOLPHIN
  bool transmitDolphinByte(uint8_t numData);
  bool receiveDolphinByte(bool canWriteOk);
  bool transmitDolphinBurst();
  bool receiveDolphinBurst();

  void startParallelTransaction();
  void endParallelTransaction();
  bool parallelBusHandshakeReceived();
  bool waitParallelBusHandshakeReceived();
  void parallelBusHandshakeTransmit();
  void setParallelBusModeInput();
  void setParallelBusModeOutput();
  bool parallelCableDetect();
  uint8_t readParallelData();
  void writeParallelData(uint8_t data);
  void enableParallelPins();
  bool isDolphinPin(uint8_t pin);

#ifdef SUPPORT_DOLPHIN_XRA1405
  uint8_t m_pinDolphinSCK, m_pinDolphinCOPI, m_pinDolphinCIPO, m_pinDolphinCS, m_inTransaction;
  uint8_t XRA1405_ReadReg(uint8_t reg);
  void    XRA1405_WriteReg(uint8_t reg, uint8_t data);

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regDolphinCS;
  IOREG_TYPE m_bitDolphinCS;
#endif

#else // !SUPPORT_DOLPHIN_XRA1405

  uint8_t m_pinDolphinParallel[8];
#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regDolphinParallelMode[8], *m_regDolphinParallelWrite[8];
  volatile const IOREG_TYPE *m_regDolphinParallelRead[8];
  IOREG_TYPE m_bitDolphinParallel[8];
#endif

#endif // SUPPORT_DOLPHIN_XRA1405

  uint8_t m_pinDolphinHandshakeTransmit;
  uint8_t m_pinDolphinHandshakeReceive;
  uint8_t m_dolphinCtr;

#ifdef IOREG_TYPE
  volatile IOREG_TYPE *m_regDolphinHandshakeTransmitMode;
  IOREG_TYPE m_bitDolphinHandshakeTransmit;
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega2560__)
  IOREG_TYPE m_handshakeReceivedBit = 0;
#endif
#endif // IOREG_TYPE
#endif // SUPPORT_DOLPHIN

#ifdef SUPPORT_EPYX
  bool receiveEpyxByte(uint8_t &data);
  bool transmitEpyxByte(uint8_t data);
  bool receiveEpyxHeader();
  bool transmitEpyxBlock();
#ifdef SUPPORT_EPYX_SECTOROPS
  bool startEpyxSectorCommand(uint8_t command);
  bool finishEpyxSectorCommand();
#endif
#endif
  
#if defined(SUPPORT_JIFFY) || defined(SUPPORT_DOLPHIN) || defined(SUPPORT_EPYX)
  uint8_t m_bufferSize;
#if IEC_DEFAULT_FASTLOAD_BUFFER_SIZE>0
#if defined(SUPPORT_EPYX) && defined(SUPPORT_EPYX_SECTOROPS)
  uint8_t  m_buffer[256];
#else
  uint8_t  m_buffer[IEC_DEFAULT_FASTLOAD_BUFFER_SIZE];
#endif
#else
  uint8_t *m_buffer;
#endif
#endif

  static IECBusHandler *s_bushandler;
  static void atnInterruptFcn(INTERRUPT_FCN_ARG);
};

#endif
