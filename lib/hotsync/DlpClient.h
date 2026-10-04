#ifndef HOTSYNC_DLP_CLIENT_H
#define HOTSYNC_DLP_CLIENT_H

// Typed DLP commands issued by the desktop side of a HotSync.
// Reference: palm-sync src/protocols/dlp-commands.ts

#include "Dlp.h"
#include "DlpTransport.h"

#include <string>
#include <vector>

struct DlpDateTime {
    uint16_t year = 0; // 0 means "never"
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
};

struct DlpSysInfo {
    uint32_t rom_version = 0;
    uint32_t localization_id = 0;
    uint16_t dlp_major = 0;
    uint16_t dlp_minor = 0;
    uint32_t max_record_size = 0;
};

struct DlpUserInfo {
    uint32_t user_id = 0;
    uint32_t viewer_id = 0;
    uint32_t last_sync_pc = 0;
    DlpDateTime successful_sync;
    DlpDateTime last_sync;
    std::string user_name;
};

// Database attribute bits (DlpDbInfo::flags).
constexpr uint16_t DLP_DB_RESOURCE = 0x0001;
constexpr uint16_t DLP_DB_READ_ONLY = 0x0002;
constexpr uint16_t DLP_DB_BACKUP = 0x0008;
constexpr uint16_t DLP_DB_RESET_AFTER_INSTALL = 0x0020;

// DlpDbInfo::misc_flags bits.
constexpr uint8_t DLP_DB_EXCLUDE_FROM_SYNC = 0x80;
constexpr uint8_t DLP_DB_RAM_BASED = 0x40;

struct DlpDbInfo {
    uint8_t misc_flags = 0;
    uint16_t flags = 0;
    char type[5] = {};
    char creator[5] = {};
    uint16_t version = 0;
    uint32_t mod_num = 0;
    DlpDateTime created;
    DlpDateTime modified;
    DlpDateTime backed_up;
    uint16_t index = 0;
    std::string name;
};

struct DlpRecord {
    uint32_t id = 0;
    uint8_t attributes = 0;
    uint8_t category = 0;
    ByteBuffer data;
};

struct DlpResource {
    char type[5] = {};
    uint16_t id = 0;
    ByteBuffer data;
};

// OpenDB mode bits.
constexpr uint8_t DLP_OPEN_READ = 0x80;
constexpr uint8_t DLP_OPEN_WRITE = 0x40;
constexpr uint8_t DLP_OPEN_EXCLUSIVE = 0x20;
constexpr uint8_t DLP_OPEN_SECRET = 0x10;

// ReadDBList search bits.
constexpr uint8_t DLP_LIST_RAM = 0x80;
constexpr uint8_t DLP_LIST_ROM = 0x40;
constexpr uint8_t DLP_LIST_MULTIPLE = 0x20;

class DlpClient
{
public:
    explicit DlpClient(DlpTransport &transport) : _transport(transport) {}

    DlpError read_sys_info(DlpSysInfo &out);
    DlpError read_user_info(DlpUserInfo &out);
    DlpError write_user_info(const DlpUserInfo &info, uint8_t mod_flags);
    // Fills out with one batch; next_index is where the following batch starts.
    DlpError read_db_list(uint8_t flags, uint16_t start, std::vector<DlpDbInfo> &out,
                          uint16_t &next_index);

    DlpError open_conduit();
    DlpError open_db(const std::string &name, uint8_t mode, uint8_t &db_handle);
    DlpError create_db(const DlpDbInfo &info, uint8_t &db_handle);
    DlpError close_db(uint8_t db_handle);
    DlpError delete_db(const std::string &name);
    DlpError read_open_db_record_count(uint8_t db_handle, uint16_t &count);

    DlpError read_app_block(uint8_t db_handle, ByteBuffer &out);
    DlpError read_sort_block(uint8_t db_handle, ByteBuffer &out);
    DlpError write_app_block(uint8_t db_handle, const ByteBuffer &data);
    DlpError write_sort_block(uint8_t db_handle, const ByteBuffer &data);

    DlpError read_record_by_index(uint8_t db_handle, uint16_t index, DlpRecord &out);
    DlpError write_record(uint8_t db_handle, const DlpRecord &record);
    DlpError read_resource_by_index(uint8_t db_handle, uint16_t index, DlpResource &out);
    DlpError write_resource(uint8_t db_handle, const DlpResource &resource);

    DlpError add_sync_log_entry(const std::string &text);
    DlpError reset_system();
    DlpError end_of_sync(uint16_t term_code = 0);

private:
    DlpError execute(const DlpRequest &request, DlpResponse &response);
    DlpError read_block(DlpFunc func, uint8_t db_handle, ByteBuffer &out);
    DlpError write_block(DlpFunc func, uint8_t db_handle, const ByteBuffer &data);

    DlpTransport &_transport;
};

// WriteUserInfo bits naming the fields to change.
constexpr uint8_t DLP_MOD_USER_ID = 0x80;
constexpr uint8_t DLP_MOD_LAST_SYNC_PC = 0x40;
constexpr uint8_t DLP_MOD_LAST_SYNC_DATE = 0x20;
constexpr uint8_t DLP_MOD_USER_NAME = 0x10;
constexpr uint8_t DLP_MOD_VIEWER_ID = 0x08;

#endif // HOTSYNC_DLP_CLIENT_H
