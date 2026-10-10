#ifndef HOTSYNC_STORAGE_H
#define HOTSYNC_STORAGE_H

#include "global_types.h"

#include <string>
#include <vector>

// Where a HotSync reads apps to install and writes backups. The firmware
// backs this with the SD card; tests back it with a host directory.
//
// Layout under the root:
//   install/              .prc/.pdb/.pqa files queued for the next sync
//   installed/            files moved here once installed
//   backup/<user>/        databases read back from the device
class HotSyncStorage
{
public:
    virtual ~HotSyncStorage() = default;

    virtual std::vector<std::string> pending_installs() = 0;
    virtual success_is_true read_install(const std::string &file_name, ByteBuffer &out) = 0;
    virtual success_is_true mark_installed(const std::string &file_name) = 0;
    virtual success_is_true write_backup(const std::string &user, const std::string &file_name,
                                         const ByteBuffer &data) = 0;
};

#endif // HOTSYNC_STORAGE_H
