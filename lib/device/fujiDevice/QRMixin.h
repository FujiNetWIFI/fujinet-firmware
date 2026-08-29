#ifndef QRMIXIN_H
#define QRMIXIN_H

#include "FujiDeviceMixin.h"
#include "../qrcode/qrmanager.h"

class QRMixin : public FujiDeviceMixin
{
private:
    FujiMixinCommandHandlers handlers = {
    };

protected:
    FujiMixinCommandHandlers commandHandlers() override { return handlers; }

    void qr_input(const FUJI_COMMAND_PACKET &packet);
    void qr_length(const FUJI_COMMAND_PACKET &packet);
    void qr_output(const FUJI_COMMAND_PACKET &packet);

    void qr_encode(uint8_t version, qr_ecc_t ecc, bool shorten);
    virtual void qr_encode(const FUJI_COMMAND_PACKET &packet) {
        qr_encode(((uint8_t) packet.param(0)) & 0x7f,
                  (qr_ecc_t) (((uint8_t) packet.param(1)) & 0x03),
                  (uint8_t) packet.param(2));
    }

public:
    QRMixin() {
        handlers = {
            { CMD::FUJI_QRCODE_INPUT,  FM_CMD_HANDLER(qr_input)  },
            { CMD::FUJI_QRCODE_ENCODE, FM_CMD_HANDLER(qr_encode) },
            { CMD::FUJI_QRCODE_LENGTH, FM_CMD_HANDLER(qr_length) },
            { CMD::FUJI_QRCODE_OUTPUT, FM_CMD_HANDLER(qr_output) },
#if 0
            // FIXME - this is missing
            { CMD::FUJI_QRCODE_CLEAR,  FM_CMD_HANDLER(qr_clear)  },
#endif
        };
    }
};

#endif /* QRMIXIN_H */
