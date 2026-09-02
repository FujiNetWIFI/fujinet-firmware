#ifndef BUS_H
#define BUS_H

#include "DaisyChain.h"
#include "global_types.h"

#include <string>

#include <type_traits>

// Compile-time check for packet.setDataLength(...)
template <typename T, typename = void>
struct has_setDataLength : std::false_type {};

template <typename T>
struct has_setDataLength<T, std::void_t<decltype(std::declval<T>().setDataLength(std::declval<uint16_t>()))>>
    : std::true_type {};

typedef enum class TRANS_STATE {
    INVALID,
    NO_GET,
    WILL_GET,
    DID_GET,
} transState_t;

/**
 * Defines the transaction contract between devices and a system bus.
 *
 * Bus implementations are responsible for all protocol-specific I/O, timing,
 * framing, and error handling. Device classes interact with the bus only
 * through this API and should not perform direct bus operations.
 *
 * A transaction is presented to the bus before transaction_accept() is called.
 * The transaction must be terminated by exactly one of transaction_send(),
 * transaction_success(), or transaction_error().
 *
 * Implementations must preserve the transaction semantics described here,
 * regardless of the underlying bus protocol.
 */
class SystemBusBase
{
protected:
    transState_t _transaction_state = TRANS_STATE::INVALID;
    DaisyChain _daisyChain;

public:
    virtual void addDevice(virtualDevice *device, fujiDeviceID_t deviceType) {
        _daisyChain.addDevice(device, deviceType);
    }
    fujiDeviceID_t fujiIDForDevice(virtualDevice *device) {
        return _daisyChain.fujiIDForDevice(device).value_or((fujiDeviceID_t) 0);
    }
    virtual void assignFujiIDToDevice(virtualDevice *device, fujiDeviceID_t fujiID) {
        _daisyChain.assignFujiIDToDevice(device, fujiID);
    }
    void setDeviceEnabled(fujiDeviceID_t device_id, bool enabled);

    // Rotate the specified devices by the given index offset.
    // Positive values increase each device's index; negative values decrease it.
    // Indices wrap around within the supplied device sequence.
    template <typename T>
    requires std::derived_from<T, virtualDevice>
    void rotateDevices(const std::vector<T *> &devices, int amount) {
        _daisyChain.rotateDevices(devices, amount);
    }

    // Accept the current transaction and perform any protocol-specific setup
    // required before data transfer.
    virtual void transaction_accept(transState_t expectMoreData) = 0;

    // Successfully complete the transaction without sending response data.
    virtual void transaction_success() = 0;

    // Terminate the transaction without sending response data due to an error.
    virtual void transaction_error() = 0;

    // Receive exactly len bytes from the current transaction. Returns false if
    // the transaction cannot be completed successfully.
    virtual success_is_true transaction_get(void *data, size_t len) = 0;

    inline success_is_true transaction_get(ByteBuffer &buffer) {
        return transaction_get(buffer.data(), buffer.size());
    }
    inline success_is_true transaction_get(std::string &buffer) {
        return transaction_get(buffer.data(), buffer.size());
    }

    // Automatically determines length of data. On systems that don't
    // support variable length packets the length is pulled from the
    // aux1/aux2 bytes in native endianness. Returns std::nullopt on
    // error.
    //
    // Automatically handles transaction_accept()
    template <typename PacketType>
    std::optional<ByteBuffer> transaction_varlen_data(const PacketType &packet) {
        ByteBuffer data;

        if constexpr (has_setDataLength<PacketType>::value) {
            uint16_t len = packet.param(0);
            data = ByteBuffer(len);
        }
        else {
            data = ByteBuffer(packet.data().value_or(ByteBuffer{}));
        }

        if (data.size())
        {
            transaction_accept(TRANS_STATE::WILL_GET);
            if (transaction_get(data).is_error())
                return std::nullopt;
        }
        else
            transaction_accept(TRANS_STATE::NO_GET);

        return data;
    }
    template <typename PacketType>
    std::optional<std::string> transaction_varlen_string(const PacketType &packet)  {
        auto buf = transaction_varlen_data(packet);
        if (buf.has_value())
            return std::string(reinterpret_cast<const char *>(buf->data()), buf->size());
        return std::nullopt;
    }

    // Send response data and complete the transaction. If is_error is true,
    // the response represents a protocol-defined error.
    virtual void transaction_send(const void *data, size_t len, bool is_error=false) = 0;

    inline void transaction_send(const std::string data, bool is_error=false) {
        transaction_send(data.data(), data.size(), is_error);
    }
    inline void transaction_send(const ByteBuffer data, bool is_error=false) {
        transaction_send(data.data(), data.size(), is_error);
    }
    inline void transaction_send(const int val) {
        uint8_t c = val;
        transaction_send(&c, sizeof(c));
    }

    enum class NativeEncoding {
        ASCII,
        PETSCII_Lower,
        PETSCII_Graphics,
        ATASCII,
        CP437,
        MSX_International,
        MSX_Japanese,
    };

    // Base: ASCII-compatible, no conversion needed.
    virtual std::string nativeTextToUnicode(const std::string &native) {
        return native;
    }
    virtual std::string unicodeTextToNative(const std::string &unicode) {
        return unicode;
    }
    virtual std::string nativeEOL() { return "\r"; }
};

#ifdef BUILD_ATARI
#include "sio/sio.h"
#ifdef ESP_PLATFORM
  #define FN_BUS_PORT fnUartBUS
#else
  #define FN_BUS_PORT fnSioCom
#endif
#endif

#ifdef BUILD_IEC
#include "iec/iec.h"
#define FN_BUS_PORT fnUartBUS  // TBD
#endif

#ifdef BUILD_ADAM
#include "adamnet/adamnet.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_LYNX
#include "comlynx/comlynx.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef NEW_TARGET
#include "new/adamnet.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_APPLE
#include "iwm/iwm.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_MAC
#include "mac/mac.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_S100
#include "s100spi/s100spi.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_RS232
#include "rs232/rs232.h"
#endif

#ifdef BUILD_CX16
#include "cx16_i2c/cx16_i2c.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_RC2014
#include "rc2014bus/rc2014bus.h"
#define FN_BUS_PORT fnUartBUS
#endif

#ifdef BUILD_H89
#include "h89/h89.h"
#define FN_BUS_PORT fnUartBUS // TBD
#endif

#ifdef BUILD_COCO
#include "drivewire/drivewire.h"
#endif

#endif // BUS_H
