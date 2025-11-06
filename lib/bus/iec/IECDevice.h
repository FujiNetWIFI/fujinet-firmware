// -----------------------------------------------------------------------------
// Copyright (C) 2024 David Hansel
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

#ifndef IECDEVICE_H
#define IECDEVICE_H

#include "IECConfig.h"
#include <stdint.h>

class IECBusHandler;

class IECDevice
{
 friend class IECBusHandler;

 public:
  // pinATN should preferrably be a pin that can handle external interrupts
  // (e.g. 2 or 3 on the Arduino UNO), if not then make sure the task() function
  // gets called at least once evey millisecond, otherwise "device not present"
  // errors may result
  IECDevice(uint8_t devnr = 0xFF);

  // call this to change the device number
  void setDeviceNumber(uint8_t devnr);

#ifdef SUPPORT_JIFFY
  // call this to enable or disable JiffyDOS support for your device.
  bool enableJiffyDosSupport(bool enable);
#endif

#ifdef SUPPORT_DOLPHIN
  // call this to enable or disable DolphinDOS support for your device.
  // this function will fail if any of the pins used for ATN/CLK/DATA/RESET
  // are the same as the pins used for the parallel cable
  bool enableDolphinDosSupport(bool enable);
#endif

#ifdef SUPPORT_EPYX
  // call this to enable or disable Expyx FastLoad support for your device.
  bool enableEpyxFastLoadSupport(bool enable);
#endif


  /**
   * @brief is device active (turned on?)
   */
  bool device_active = true;

  // this can be overloaded by derived classes
  virtual bool isActive() { return device_active; }

  // if isActive() is not overloaded then use this to activate/deactivate a device
  void setActive(bool b) { device_active = b; }

 protected:
  // called when IECBusHandler::begin() is called
  virtual void begin() {}

  // called IECBusHandler::task() is called
  virtual void task()  {}

  // called on falling edge of RESET line
  virtual void reset() {}

  // called when bus master sends TALK command
  // talk() must return within 1 millisecond
  virtual void talk(uint8_t secondary)   {}

  // called when bus master sends LISTEN command
  // listen() must return within 1 millisecond
  virtual void listen(uint8_t secondary) {}

  // called when bus master sends UNTALK command
  // untalk() must return within 1 millisecond
  virtual void untalk() {}

  // called when bus master sends UNLISTEN command
  // unlisten() must return within 1 millisecond
  virtual void unlisten() {}

  // called before a write() call to determine whether the device
  // is ready to receive data.
  // canWrite() is allowed to take an indefinite amount of time
  // canWrite() should return:
  //  <0 if more time is needed before data can be accepted (call again later), blocks IEC bus
  //   0 if no data can be accepted (error)
  //  >0 if at least one byte of data can be accepted
  virtual int8_t canWrite() { return 0; }

  // called before a read() call to see how many bytes are available to read
  // canRead() is allowed to take an indefinite amount of time
  // canRead() should return:
  //  <0 if more time is needed before we can read (call again later), blocks IEC bus
  //   0 if no data is available to read (error)
  //   1 if one byte of data is available
  //  >1 if more than one byte of data is available
  virtual int8_t canRead() { return 0; }

  // called when the device received data
  // write() will only be called if the last call to canWrite() returned >0
  // write() must return within 1 millisecond
  // the "eoi" parameter will be "true" if sender signaled that this is the last
  // data byte of a transmission
  virtual void write(uint8_t data, bool eoi) {}

  // called when the device is sending data
  // read() will only be called if the last call to canRead() returned >0
  // read() is allowed to take an indefinite amount of time
  virtual uint8_t read() { return 0; }

#if defined(SUPPORT_JIFFY) || defined(SUPPORT_DOLPHIN)
  // called when the device is sending data using JiffyDOS byte-by-byte protocol
  // peek() will only be called if the last call to canRead() returned >0
  // peek() should return the next character that will be read with read()
  // peek() is allowed to take an indefinite amount of time
  virtual uint8_t peek() { return 0; }
#endif

#ifdef SUPPORT_DOLPHIN
  // called when the device is sending data using the DolphinDos burst transfer (SAVE protocol)
  // should write all the data in the buffer and return the number of bytes written
  // returning less than bufferSize signals an error condition
  // the "eoi" parameter will be "true" if sender signaled that this is the final part of the transmission
  // write() is allowed to take an indefinite amount of time
  // the default implementation within IECDevice uses the canWrite() and write(data,eoi) functions,
  // which is not efficient.
  // it is highly recommended to override this function in devices supporting DolphinDos
  virtual uint8_t write(uint8_t *buffer, uint8_t bufferSize, bool eoi);
#endif

#if defined(SUPPORT_JIFFY) || defined(SUPPORT_DOLPHIN) || defined(SUPPORT_EPYX)
  // called when the device is sending data using the JiffyDOS block transfer
  // or DolphinDos burst transfer (LOAD protocols)
  // - should fill the buffer with as much data as possible (up to bufferSize)
  // - must return the number of bytes put into the buffer
  // read() is allowed to take an indefinite amount of time
  // the default implementation within IECDevice uses the canRead() and read() functions,
  // which is not efficient.
  // it is highly recommended to override this function in devices supporting JiffyDos or DolphinDos.
  virtual uint8_t read(uint8_t *buffer, uint8_t bufferSize);
#endif

#if defined(SUPPORT_EPYX) && defined(SUPPORT_EPYX_SECTOROPS)
  // these functions are experimental, they are called when the Epyx Cartridge uses
  // sector read/write operations (disk editor, disk copy or file copy).
  virtual bool epyxReadSector(uint8_t track, uint8_t sector, uint8_t *buffer)  { return false; }
  virtual bool epyxWriteSector(uint8_t track, uint8_t sector, uint8_t *buffer) { return false; }
#endif

#ifdef SUPPORT_DOLPHIN
  // call this to enable or disable DolphinDOS burst transmission mode
  // On the 1541, this gets enabled/disabled by the "XF+"/"XF-" command
  // (the IECFileDevice class handles this automatically)
  void enableDolphinBurstMode(bool enable);

  // call this when a DolphinDOS burst recive request ("XZ") is received
  // on the command channel (the IECFileDevice class handles this automatically)
  void dolphinBurstReceiveRequest();

  // call this when a DolphinDOS burst transmit request ("XQ") is received
  // on the command channel (the IECFileDevice class handles this automatically)
  void dolphinBurstTransmitRequest();
#endif

#ifdef SUPPORT_EPYX
  // call this after receiving the EPYX fast-load routine upload (via M-W and M-E)
  // (the IECFileDevice class handles this automatically)
  void epyxLoadRequest();
#endif

  // send pulse on SRQ line (if SRQ pin was set in IECBusHandler constructor)
  void sendSRQ();

 protected:
  //bool       m_isActive;
  uint8_t    m_devnr;
  uint16_t m_sflags;
  IECBusHandler *m_handler;
};

/* Compatibility with how devices work on other platforms, needed for fujiDevice */
class virtualDevice : public IECDevice
{
protected:
    uint8_t status_wait_count = 5;

    virtual void shutdown() {};

};

#endif
