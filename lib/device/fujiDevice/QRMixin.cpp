#include "QRMixin.h"

#ifdef FUJI_QR_MIXIN_ENABLED

#include "httpService.h"
#include "debug.h"

void QRMixin::qr_input(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

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

void QRMixin::qr_encode(uint8_t version, qr_ecc_t ecc, bool shorten)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("QRMixin: ENCODE (version: %d, ecc: %d, shorten: %s)\n", version, ecc, shorten ? "Y" : "N");

    if (shorten)
        qrManager.data = fnHTTPD.shorten_url(qrManager.data);

    qrManager.version(version);
    qrManager.ecc(ecc);
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

void QRMixin::qr_length(const FUJI_COMMAND_PACKET &packet)
{
    uint8_t output_mode = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("QRMixin: LENGTH (output mode: %d)\n", output_mode);

    u32ne_t len;
    len = qrManager.code.size();

    // Changing output mode re-renders the existing matrix into the new format.
    if (len && (output_mode != qrManager.output_mode)) {
        qrManager.output_mode = (ouput_mode_t)output_mode;
        qrManager.render();
        len = qrManager.code.size();
    }

    Debug_printf("QR code buffer length: %u bytes\n", (size_t) len);

    SYSTEM_BUS.transaction_send(&len, sizeof(len), false);
}

void QRMixin::qr_output(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

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

#endif // FUJI_QR_MIXIN_ENABLED
