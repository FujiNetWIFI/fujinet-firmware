#ifndef HOTSYNC_DLP_TRANSPORT_H
#define HOTSYNC_DLP_TRANSPORT_H

#include "global_types.h"

#include <cstdint>

// Carries whole DLP messages. PADP (serial) and NetSync (TCP) implement it.
class DlpTransport
{
public:
    virtual ~DlpTransport() = default;

    // Runs the transport-level handshake that precedes the first DLP request.
    virtual success_is_true accept() = 0;
    virtual success_is_true send(const ByteBuffer &message) = 0;
    virtual success_is_true receive(ByteBuffer &message, uint32_t timeout_ms) = 0;
};

#endif // HOTSYNC_DLP_TRANSPORT_H
