#ifndef SEGMENTCARTRIDGE_H
#define SEGMENTCARTRIDGE_H

#include "Cartridge.h"

// OPEN's param is the load segment (u16). Spoken by pico/fujiversal (CoCo).
// Any reply counts as taken: that firmware can NAK and then ACK one WRITE.
class SegmentCartridge : public Cartridge
{
public:
    explicit SegmentCartridge(DBCLink &link) : Cartridge(link) {}

    // Pushes `image` to segment 0; its CLOSE boots it.
    success_is_true load(const Source &image);

protected:
    success_is_true accepted(const FujiBusPacket *reply) const override;

private:
    static constexpr size_t BLOCK = 256;
};

#endif /* SEGMENTCARTRIDGE_H */
