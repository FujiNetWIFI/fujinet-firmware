#if defined(BUILD_IEC) && !defined(ESP_PLATFORM)

#include "IECBusHandler.h"
#include "IECDevice.h"

namespace
{
constexpr uint8_t BUS_NOT_BEGUN = 0xFF;
constexpr uint8_t BUS_LISTENING = 0x40;
constexpr uint8_t BUS_TALKING   = 0x20;
constexpr uint8_t BUS_DONE      = 0x10;

constexpr uint8_t ATN_COMMAND_MASK = 0xE0;
constexpr uint8_t ATN_DEVICE_MASK  = 0x1F;
constexpr uint8_t ATN_LISTEN       = 0x20;
constexpr uint8_t ATN_TALK         = 0x40;
constexpr uint8_t ATN_UNLISTEN     = 0x3F;
constexpr uint8_t ATN_UNTALK       = 0x5F;
} // namespace

IECBusHandler::IECBusHandler(uint8_t pinATN, uint8_t pinCLK, uint8_t pinDATA, uint8_t pinRESET,
                             uint8_t pinCTRL, uint8_t pinSRQ)
{
  m_currentDevice = nullptr;
  m_numDevices    = 0;
  m_atnInterrupt  = -1;
  m_inTask        = false;
  m_flags         = BUS_NOT_BEGUN;

  m_pinATN   = pinATN;
  m_pinCLK   = pinCLK;
  m_pinDATA  = pinDATA;
  m_pinRESET = pinRESET;
  m_pinCTRL  = pinCTRL;
  m_pinSRQ   = pinSRQ;
}

void IECBusHandler::begin()
{
  m_flags = 0;
  for (uint8_t i = 0; i < m_numDevices; i++)
    m_devices[i]->begin();
}

bool IECBusHandler::attachDevice(IECDevice *dev)
{
  if (m_numDevices >= MAX_DEVICES || findDevice(dev->m_devnr, true) != nullptr)
    return false;

  dev->m_handler = this;
  if (m_flags != BUS_NOT_BEGUN)
    dev->begin();

  m_devices[m_numDevices++] = dev;
  return true;
}

bool IECBusHandler::detachDevice(IECDevice *dev)
{
  for (uint8_t i = 0; i < m_numDevices; i++)
    if (dev == m_devices[i])
    {
      dev->m_handler = nullptr;
      m_devices[i] = m_devices[m_numDevices - 1];
      m_numDevices--;
      if (m_currentDevice == dev)
        m_currentDevice = nullptr;
      return true;
    }

  return false;
}

IECDevice *IECBusHandler::findDevice(uint8_t devnr, bool includeInactive)
{
  for (uint8_t i = 0; i < m_numDevices; i++)
    if (devnr == m_devices[i]->m_devnr && (includeInactive || m_devices[i]->isActive()))
      return m_devices[i];

  return nullptr;
}

// The bus master waits for every NetIEC reply, so devices may do slow work in task()
bool IECBusHandler::canServeATN()
{
  return true;
}

bool IECBusHandler::inTransaction()
{
  return m_flags != BUS_NOT_BEGUN && (m_flags & (BUS_LISTENING | BUS_TALKING)) != 0;
}

void IECBusHandler::sendSRQ()
{
}

void IECBusHandler::task()
{
  if (m_flags == BUS_NOT_BEGUN)
    return;

  for (uint8_t i = 0; i < m_numDevices; i++)
    m_devices[i]->task();
}

success_is_true IECBusHandler::atnCommand(uint8_t primary, uint8_t secondary)
{
  if (m_flags == BUS_NOT_BEGUN)
    RETURN_ERROR_AS_FALSE();

  m_flags &= ~BUS_DONE;
  m_currentDevice = nullptr;

  if (primary == ATN_UNLISTEN)
  {
    if (m_flags & BUS_LISTENING)
    {
      m_flags &= ~BUS_LISTENING;
      for (uint8_t i = 0; i < m_numDevices; i++)
        m_devices[i]->unlisten();
    }
    RETURN_SUCCESS_AS_TRUE();
  }

  if (primary == ATN_UNTALK)
  {
    if (m_flags & BUS_TALKING)
    {
      m_flags &= ~BUS_TALKING;
      for (uint8_t i = 0; i < m_numDevices; i++)
        m_devices[i]->untalk();
    }
    RETURN_SUCCESS_AS_TRUE();
  }

  uint8_t command = primary & ATN_COMMAND_MASK;
  IECDevice *dev = findDevice(primary & ATN_DEVICE_MASK);
  if (dev == nullptr || (command != ATN_LISTEN && command != ATN_TALK))
  {
    // Another device was told to talk, so ours must stop
    if (command == ATN_TALK)
      m_flags &= ~BUS_TALKING;
    RETURN_ERROR_AS_FALSE();
  }

  m_currentDevice = dev;
  if (command == ATN_LISTEN)
  {
    dev->listen(secondary);
    m_flags = (m_flags & ~BUS_TALKING) | BUS_LISTENING;
  }
  else
  {
    dev->talk(secondary);
    m_flags = (m_flags & ~BUS_LISTENING) | BUS_TALKING;
  }
  RETURN_SUCCESS_AS_TRUE();
}

int8_t IECBusHandler::listenerCanWrite()
{
  if ((m_flags & (BUS_LISTENING | BUS_DONE)) != BUS_LISTENING || m_currentDevice == nullptr)
    return 0;

  int8_t numData = m_currentDevice->canWrite();
  if (numData == 0)
    m_flags |= BUS_DONE;
  return numData;
}

void IECBusHandler::listenerWrite(uint8_t data, bool eoi)
{
  if (m_currentDevice != nullptr)
    m_currentDevice->write(data, eoi);
}

int8_t IECBusHandler::talkerCanRead()
{
  if ((m_flags & (BUS_TALKING | BUS_DONE)) != BUS_TALKING || m_currentDevice == nullptr)
    return 0;

  int8_t numData = m_currentDevice->canRead();
  if (numData == 0)
    m_flags |= BUS_DONE;
  return numData;
}

uint8_t IECBusHandler::talkerRead()
{
  return m_currentDevice != nullptr ? m_currentDevice->read() : 0;
}

void IECBusHandler::busReset()
{
  if (m_flags == BUS_NOT_BEGUN)
    return;

  m_flags = 0;
  m_currentDevice = nullptr;
  for (uint8_t i = 0; i < m_numDevices; i++)
    m_devices[i]->reset();
}

uint32_t IECBusHandler::deviceMask()
{
  uint32_t mask = 0;
  for (uint8_t i = 0; i < m_numDevices; i++)
    if (m_devices[i]->isActive() && m_devices[i]->m_devnr < 32)
      mask |= 1u << m_devices[i]->m_devnr;
  return mask;
}

#endif /* BUILD_IEC && !ESP_PLATFORM */
