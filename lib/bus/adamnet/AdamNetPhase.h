#ifndef ADAMNETPHASE_H
#define ADAMNETPHASE_H

#ifdef BUILD_ADAM

#include "FujiAdamPacket.h"

class AdamNetPhase
{
public:
    enum class PHASE {
        IDLE,
        NEED_ACK,
        DID_ACK,
        DID_NAK,
        DID_IGNORE,
        HAVE_PAYLOAD,
        GOT_PAYLOAD,
        NEED_STATUS,
        SENT_STATUS,
        FEED_ME,
        BEEN_FED,
    };

    void begin(const FujiAdamPacket &packet);
    void finish();

    void didAck();
    void didNak();
    void didIgnore();

    void sentStatus();
    void sentData();

    void gotPayload();

    bool needAck() { return _busPhase == PHASE::NEED_ACK; }

    static const char* type_to_string(adamPacketType_t type);
    static const char* device_to_string(fujiDeviceID_t device);

    PHASE phase() { return _busPhase; }

private:
    PHASE _busPhase = PHASE::IDLE;
    const FujiAdamPacket *_packet;

    const char* phase_to_string(PHASE state);
    bool bus_expected_state(const FujiAdamPacket &packet, PHASE actual,
                            std::initializer_list<PHASE> expected_list);
};

#endif /* BUILD_ADAM */

#endif /* ADAMNETPHASE_H */
