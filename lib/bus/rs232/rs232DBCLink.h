#ifdef BUILD_RS232
#ifndef RS232DBCLINK_H
#define RS232DBCLINK_H

#include "DBCLink.h"

// DBC frames go out on the same link we're answering the companion on.
class rs232DBCLink : public DBCLink
{
protected:
    std::unique_ptr<FujiBusPacket> exchange(FujiBusPacket &packet) override;
};

#endif /* RS232DBCLINK_H */
#endif /* BUILD_RS232 */
