#include "IWMBusIDMap.h"

void IWMBusIDMap::addFujiID(fujiDeviceID_t fujiID, bool participatesInBusIDAssignment)
{
  _entries[fujiID] = {std::nullopt, participatesInBusIDAssignment, };
  return;
}

std::optional<fujiDeviceID_t> IWMBusIDMap::fujiIDForBusID(busDeviceID_t busID)
{
  for (const auto &[fujiID, entry] : _entries)
  {
    if (entry.busID && *entry.busID == busID)
      return fujiID;
  }

  return std::nullopt;
}

std::optional<IWMBusIDMap::busDeviceID_t> IWMBusIDMap::busIDForFujiID(fujiDeviceID_t fujiID)
{
  auto it = _entries.find(fujiID);

  if (it == _entries.end())
    return std::nullopt;

  return it->second.busID;
}

void IWMBusIDMap::resetAllBusIDs()
{
  for (auto &[fujiID, entry] : _entries)
    entry.busID.reset();
  return;
}

void IWMBusIDMap::assignBusIDToFujiID(busDeviceID_t busID, fujiDeviceID_t fujiID)
{
  fujiDeviceID_t first, last;

  first = last = fujiID;
  if (fujiID >= FUJI_DEVICEID::NETWORK && fujiID <= FUJI_DEVICEID::NETWORK_LAST)
  {
    first = FUJI_DEVICEID::NETWORK;
    last = FUJI_DEVICEID::NETWORK_LAST;
  }

  for (int idx = static_cast<int>(first); idx <= static_cast<int>(last); idx++)
  {
    fujiID = static_cast<fujiDeviceID_t>(idx);
    auto it = _entries.find(fujiID);
    if (it != _entries.end())
      it->second.busID = busID;
  }

  return;
}

bool IWMBusIDMap::participatesInBusIDAssignment(fujiDeviceID_t fujiID)
{
  auto it = _entries.find(fujiID);

  if (it == _entries.end())
    return false;

  return it->second.participatesInBusIDAssignment;
}

void IWMBusIDMap::changeFujiID(fujiDeviceID_t oldFujiID, fujiDeviceID_t newFujiID)
{
  if (oldFujiID == newFujiID)
    return;

  auto it = _entries.find(oldFujiID);

  if (it == _entries.end())
    return;

  auto entry = it->second;
  _entries.erase(it);
  _entries[newFujiID] = entry;
}
