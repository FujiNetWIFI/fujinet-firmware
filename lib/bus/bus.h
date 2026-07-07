#ifndef BUS_H
#define BUS_H

#include "global_types.h"

#include <string>

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

public:
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

    // Send response data and complete the transaction. If is_error is true,
    // the response represents a protocol-defined error.
    virtual void transaction_send(const void *data, size_t len, bool is_error=false) = 0;

    inline void transaction_send(std::string data, bool is_error=false) {
        transaction_send(data.data(), data.size(), is_error);
    }
    inline void transaction_send(ByteBuffer data, bool is_error=false) {
        transaction_send(data.data(), data.size(), is_error);
    }
};

// Temporary migration wrappers. Remove after all buses have been
// converted to inherit from SystemBusBase.
class VDevMigrationWrapper
{
#if defined(BUILD_RS232)
protected:
    void transaction_begin(transState_t expectMoreData);
    void transaction_complete();
    void transaction_error();
    success_is_true transaction_get(void *data, size_t len);
    void transaction_put(const void *data, size_t len, bool is_error=false);
#endif
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
