#ifndef DBCLINK_H
#define DBCLINK_H

#include <memory>
#include <utility>

#include "rs232/FujiBusPacket.h"

// Our end of the link to the companion MCU (FUJI_DEVICEID::DBC). Knows only
// requests and replies, not what they carry. Each bus that has a companion
// implements exchange().
class DBCLink
{
public:
    virtual ~DBCLink() = default;

    // The companion's reply, ACK or not; nullptr if none came back.
    template <typename... Args>
    std::unique_ptr<FujiBusPacket> request(fujiCommandID_t command, Args &&...args)
    {
        FujiBusPacket packet(FUJI_DEVICEID::DBC, command, std::forward<Args>(args)...);
        return exchange(packet);
    }

protected:
    virtual std::unique_ptr<FujiBusPacket> exchange(FujiBusPacket &packet) = 0;
};

#endif /* DBCLINK_H */
