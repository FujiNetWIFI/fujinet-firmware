#ifdef BUILD_COCO
#ifndef DRIVEWIREDBCLINK_H
#define DRIVEWIREDBCLINK_H

#include "DBCLink.h"

// DBC frames share the serial link with DriveWire traffic. Nothing is sent
// over BoIP or on a board whose pinmap lacks FN_DBC_COMPANION.
class drivewireDBCLink : public DBCLink
{
protected:
    std::unique_ptr<FujiBusPacket> exchange(FujiBusPacket &packet) override;
};

#endif /* DRIVEWIREDBCLINK_H */
#endif /* BUILD_COCO */
