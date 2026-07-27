#ifndef QRMIXIN_H
#define QRMIXIN_H

#include "FujiDeviceMixin.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_QR_MIXIN_ENABLED

class QRMixin : public FujiDeviceMixin
{
protected:
    void qr_input(uint16_t len);
    void qr_encode(uint8_t version, uint8_t ecc_raw, uint8_t shorten_raw);
    void qr_length(uint8_t output_mode);
    void qr_output(uint16_t len);

public:
    bool processCommand(const FUJI_COMMAND_PACKET &packet) override;
};

#endif // FUJI_MIXINS_ENABLED

#endif /* QRMIXIN_H */
