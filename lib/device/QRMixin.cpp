#include "QRMixin.h"

#ifdef FUJI_QR_MIXIN_ENABLED

#include "httpService.h"
#include "debug.h"
#include "../qrcode/qrmanager.h"

void QRMixin::qr_input(uint16_t len)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    Debug_printf("QRMixin: INPUT (len: %d)\n", len);

    if (!len)
    {
        Debug_printf("Invalid length. Aborting");
        SYSTEM_BUS.transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    SYSTEM_BUS.transaction_get(p.data(), len);
    qrManager.data += std::string((const char *)p.data(), len);
    SYSTEM_BUS.transaction_success();
}

void QRMixin::qr_encode(uint8_t version, uint8_t ecc_raw, uint8_t shorten_raw)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    // Platforms with 3 free params send ecc and shorten as separate bytes; those
    // limited to 2 params merge shorten into the high nibble of the ecc byte.
    // Accept either so every bus's client encoding works.
    version &= 0x7f;
    uint8_t ecc = ecc_raw & 0x03;
    bool shorten = (shorten_raw & 0x01) || ((ecc_raw >> 4) & 0x01);

    Debug_printf("QRMixin: ENCODE (version: %d, ecc: %d, shorten: %s)\n", version, ecc, shorten ? "Y" : "N");

    if (shorten)
        qrManager.data = fnHTTPD.shorten_url(qrManager.data);

    qrManager.version(version);
    qrManager.ecc((qr_ecc_t)ecc);
    qrManager.output_mode = QR_OUTPUT_MODE_BINARY;
    qrManager.encode();

    // Clear input buffer. Re-rendering only needs the encoded matrix,
    // so qr_length() should still work after this.
    qrManager.data.clear();

    if (!qrManager.code.size())
    {
        Debug_printf("QR code encoding failed\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    Debug_printf("Resulting QR code is: %u modules\n", qrManager.code.size());
    SYSTEM_BUS.transaction_success();
}

void QRMixin::qr_length(uint8_t output_mode)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("QRMixin: LENGTH (output mode: %d)\n", output_mode);

    size_t len = qrManager.code.size();

    // Changing output mode re-renders the existing matrix into the new format.
    if (len && (output_mode != qrManager.output_mode)) {
        qrManager.output_mode = (ouput_mode_t)output_mode;
        qrManager.render();
        len = qrManager.code.size();
    }

    Debug_printf("QR code buffer length: %u bytes\n", len);

#ifdef BUILD_COCO
    uint32_t response = htobe32(len);
#else
    uint32_t response = htole32(len);
#endif
    SYSTEM_BUS.transaction_send(&response, sizeof(response), false);
}

void QRMixin::qr_output(uint16_t len)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("QRMixin: OUTPUT (len: %d)\n", len);

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }
    else if (len > qrManager.code.size())
    {
        Debug_printf("Requested %u bytes, but buffer is only %u bytes, aborting.\n", len, qrManager.code.size());
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_send(qrManager.code.data(), len, false);
    qrManager.code.erase(qrManager.code.begin(), qrManager.code.begin() + len);
    qrManager.code.shrink_to_fit();
}

bool QRMixin::processCommand(const FUJI_COMMAND_PACKET &packet)
{
    switch (packet.command())
    {
    case FUJICMD_QRCODE_INPUT:
        qr_input(packet.param(0));
        break;
    case FUJICMD_QRCODE_ENCODE:
#ifdef BUILD_ATARI
        // SIO only has aux1/aux2 so shorten is store in high nybble of 2nd param
        qr_encode(packet.param(0), packet.param(1), (uint8_t)packet.param(1)>>4);
#else
        qr_encode(packet.param(0), packet.param(1), packet.param(2));
#endif
        break;
    case FUJICMD_QRCODE_LENGTH:
        qr_length(packet.param(0));
        break;
    case FUJICMD_QRCODE_OUTPUT:
        qr_output(packet.param(0));
        break;

    default:
        return false;
    }

    return true;
}

#endif // FUJI_QR_MIXIN_ENABLED
