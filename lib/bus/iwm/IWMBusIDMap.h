#ifndef BUSIDMAP_H
#define BUSIDMAP_H

#include "fujiDeviceID.h"

#include <optional>
#include <map>

class IWMBusIDMap
{
public:
    using busDeviceID_t = unsigned;

private:
  struct Entry {
    std::optional<busDeviceID_t> busID;
    bool participatesInBusIDAssignment;
  };

  std::map<fujiDeviceID_t, Entry> _entries;

public:
  void addFujiID(fujiDeviceID_t fujiID, bool participatesInBusIDAssignment);
  std::optional<fujiDeviceID_t> fujiIDForBusID(busDeviceID_t busID);
  std::optional<busDeviceID_t> busIDForFujiID(fujiDeviceID_t fujiID);
  void resetAllBusIDs();
  void assignBusIDToFujiID(busDeviceID_t busID, fujiDeviceID_t fujiID);
  bool participatesInBusIDAssignment(fujiDeviceID_t fujiID);
  void changeFujiID(fujiDeviceID_t oldFujiID, fujiDeviceID_t newFujiID);
};

#endif /* BUSIDMAP_H */
