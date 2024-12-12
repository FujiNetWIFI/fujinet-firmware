#include <_types/_uint8_t.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>
#include "qrmanager.h"
#include "qrcode.h"

QRManager qrManager;

std::vector<uint8_t> QRManager::encode(const void* src, size_t len, size_t version, size_t ecc, size_t *out_len) {
    QRCode qr_code;
    uint8_t qr_bytes[qrcode_getBufferSize(version)];

    qrcode_initText(&qr_code, qr_bytes, version, ecc, (const char*)src);

    size_t size = qr_code.size;
    *out_len = size*size;

    qrManager.qr_output.clear();
    qrManager.qr_output.shrink_to_fit();

    for (uint8_t x = 0; x < size; x++) {
        for (uint8_t y = 0; y < size; y++) {
            uint8_t on = qrcode_getModule(&qr_code, x, y);
            qrManager.qr_output.push_back(on);
        }
    }

    return qrManager.qr_output;
}
