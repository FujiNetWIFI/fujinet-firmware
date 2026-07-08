#include "bus.h"

// Temporary migration wrappers. Remove after all buses have been
// converted to inherit from SystemBusBase.
#if defined(BUILD_RS232) || defined(BUILD_COCO)

void VDevMigrationWrapper::transaction_begin(transState_t expectMoreData)
{
    SYSTEM_BUS.transaction_accept(expectMoreData);
}

void VDevMigrationWrapper::transaction_complete()
{
    SYSTEM_BUS.transaction_success();
}

void VDevMigrationWrapper::transaction_error()
{
    SYSTEM_BUS.transaction_error();
}

success_is_true VDevMigrationWrapper::transaction_get(void *data, size_t len)
{
    return SYSTEM_BUS.transaction_get(data, len);
}

void VDevMigrationWrapper::transaction_put(const void *data, size_t len, bool is_error)
{
    SYSTEM_BUS.transaction_send(data, len, is_error);
}

#endif
