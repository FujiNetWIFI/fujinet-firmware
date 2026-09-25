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
    void sentData(size_t len);

    void gotPayload();

    bool needAck() { return _busPhase == PHASE::NEED_ACK; }

    static const char* type_to_string(adamPacketType_t type);
    static const char* device_to_string(fujiDeviceID_t device);

    PHASE phase() { return _busPhase; }

private:
    PHASE _busPhase = PHASE::IDLE;
    const FujiAdamPacket *_packet;

    // Recent bus events: recorded without I/O, printed only when a state
    // assertion fails.
    enum TraceEvent : uint8_t {
        TRACE_ACK = 0x10, // above the 0x0-0xF packet types
        TRACE_NAK,
        TRACE_IGNORE,
        TRACE_SENT_STATUS,
        TRACE_SENT_DATA,
        TRACE_GOT_PAYLOAD,
    };
    struct TraceEntry {
        uint32_t usec;
        uint16_t len;
        uint8_t device;
        uint8_t event;
        uint8_t phase;
    };
    static constexpr size_t TRACE_LEN = 32;
    TraceEntry _trace[TRACE_LEN] = {};
    size_t _traceNext = 0;
    size_t _traceCount = 0;

    void trace(uint8_t event, size_t len = 0);
    void dumpTrace();

    const char* phase_to_string(PHASE state);
    bool bus_expected_state(const FujiAdamPacket &packet, PHASE actual,
                            std::initializer_list<PHASE> expected_list);
};

#endif /* BUILD_ADAM */

#endif /* ADAMNETPHASE_H */
