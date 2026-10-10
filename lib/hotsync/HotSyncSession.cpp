#include "HotSyncSession.h"

#include "../../include/debug.h"

#include <algorithm>
#include <cctype>

// Databases every device regenerates; backing them up only wastes time.
static const char *const SKIP_BACKUP[] = {"Unsaved Preferences"};

static bool has_install_extension(const std::string &file_name)
{
    size_t dot = file_name.rfind('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = file_name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "prc" || ext == "pdb" || ext == "pqa";
}

std::string hotsync_safe_name(const std::string &name)
{
    std::string safe;
    for (char c : name)
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' ||
                 c == '.')
                    ? c
                    : '_';
    return safe;
}

std::string hotsync_backup_file_name(const DlpDbInfo &info)
{
    return hotsync_safe_name(info.name) + ((info.flags & DLP_DB_RESOURCE) ? ".prc" : ".pdb");
}

HotSyncSession::HotSyncSession(DlpTransport &transport, HotSyncStorage &storage,
                               const HotSyncOptions &options)
    : _dlp(transport), _storage(storage), _options(options)
{
}

HotSyncReport HotSyncSession::run()
{
    DlpError err = identify_user();
    if (err == DlpError::NONE)
        err = _dlp.open_conduit();
    if (err == DlpError::NONE)
        err = install_pending();
    if (err == DlpError::NONE)
        err = backup_databases();
    if (err == DlpError::NONE)
        err = finish();
    else if (err != DlpError::TRANSPORT)
        _dlp.end_of_sync(err == DlpError::CANCEL_SYNC ? 2 : 3);

    _report.error = err;
    _report.user_name = _user.user_name;
    Debug_printf("HotSync: done for '%s': %d installed, %d backed up, result %s\r\n",
                 _report.user_name.c_str(), _report.installed, _report.backed_up,
                 dlp_error_name(err));
    return _report;
}

DlpError HotSyncSession::identify_user()
{
    DlpSysInfo sys;
    DlpError err = _dlp.read_sys_info(sys);
    if (err != DlpError::NONE)
        return err;
    err = _dlp.read_user_info(_user);
    if (err != DlpError::NONE)
        return err;

    if (_user.user_id == 0 || _user.user_name.empty())
    {
        _user_is_new = true;
        _user.user_id = _options.new_user_id;
        _user.user_name = _options.default_user_name;
    }
    Debug_printf("HotSync: device ROM %08lx, DLP %u.%u, user '%s'%s\r\n",
                 static_cast<unsigned long>(sys.rom_version), sys.dlp_major, sys.dlp_minor,
                 _user.user_name.c_str(), _user_is_new ? " (new)" : "");
    return DlpError::NONE;
}

DlpError HotSyncSession::install_pending()
{
    for (const std::string &file_name : _storage.pending_installs())
    {
        if (!has_install_extension(file_name))
            continue;
        DlpError err = install_file(file_name);
        if (is_fatal(err))
            return err;
        if (err == DlpError::NONE)
        {
            ++_report.installed;
            _storage.mark_installed(file_name);
            log("Installed " + file_name);
        }
        else
        {
            ++_report.install_failures;
            log("Install failed: " + file_name + " (" + dlp_error_name(err) + ")");
        }
    }
    return DlpError::NONE;
}

DlpError HotSyncSession::install_file(const std::string &file_name)
{
    ByteBuffer file;
    PalmDatabase db;
    if (_storage.read_install(file_name, file).is_error() || db.parse(file).is_error())
    {
        Debug_printf("HotSync: cannot read %s\r\n", file_name.c_str());
        return DlpError::PARAM;
    }
    Debug_printf("HotSync: installing %s as '%s' (%s/%s)\r\n", file_name.c_str(),
                 db.info.name.c_str(), db.info.type, db.info.creator);
    return write_database(db);
}

DlpError HotSyncSession::write_database(const PalmDatabase &db)
{
    // Replace any existing copy, as Palm Desktop's install conduit does.
    DlpError err = _dlp.delete_db(db.info.name);
    if (err != DlpError::NONE && err != DlpError::NOT_FOUND)
        return err;

    uint8_t handle = 0;
    err = _dlp.create_db(db.info, handle);
    if (err != DlpError::NONE)
        return err;

    err = write_contents(handle, db);
    DlpError close_err = _dlp.close_db(handle);
    if (err == DlpError::NONE)
        err = close_err;

    if (err == DlpError::NONE &&
        ((db.info.flags & DLP_DB_RESET_AFTER_INSTALL) || std::memcmp(db.info.creator, "ptch", 4) == 0))
        _reset_after_sync = true;
    return err;
}

DlpError HotSyncSession::write_contents(uint8_t handle, const PalmDatabase &db)
{
    DlpError err = DlpError::NONE;
    if (!db.app_info.empty())
        err = _dlp.write_app_block(handle, db.app_info);
    if (err == DlpError::NONE && !db.sort_info.empty())
        err = _dlp.write_sort_block(handle, db.sort_info);

    for (size_t i = 0; err == DlpError::NONE && i < db.resources.size(); ++i)
    {
        err = _dlp.write_resource(handle, db.resources[i]);
        if (std::memcmp(db.resources[i].type, "boot", 4) == 0)
            _reset_after_sync = true;
    }
    for (size_t i = 0; err == DlpError::NONE && i < db.records.size(); ++i)
        err = _dlp.write_record(handle, db.records[i]);
    return err;
}

DlpError HotSyncSession::backup_databases()
{
    if (_options.backup == HotSyncBackup::NONE)
        return DlpError::NONE;

    std::vector<DlpDbInfo> databases;
    uint16_t start = 0;
    for (;;)
    {
        uint16_t next = 0;
        DlpError err = _dlp.read_db_list(DLP_LIST_RAM | DLP_LIST_MULTIPLE, start, databases, next);
        if (err == DlpError::NOT_FOUND)
            break;
        if (err != DlpError::NONE)
            return err;
        start = next;
    }

    for (const DlpDbInfo &info : databases)
    {
        if (_options.backup == HotSyncBackup::FLAGGED && !(info.flags & DLP_DB_BACKUP))
            continue;
        if (std::find(std::begin(SKIP_BACKUP), std::end(SKIP_BACKUP), info.name) != std::end(SKIP_BACKUP))
            continue;
        DlpError err = backup_database(info);
        if (is_fatal(err))
            return err;
        if (err == DlpError::NONE)
            ++_report.backed_up;
        else
        {
            ++_report.backup_failures;
            Debug_printf("HotSync: backup of '%s' failed: %s\r\n", info.name.c_str(), dlp_error_name(err));
        }
    }
    if (_report.backed_up > 0)
        log("Backed up " + std::to_string(_report.backed_up) + " databases");
    return DlpError::NONE;
}

DlpError HotSyncSession::backup_database(const DlpDbInfo &info)
{
    uint8_t handle = 0;
    DlpError err = _dlp.open_db(info.name, DLP_OPEN_READ | DLP_OPEN_SECRET, handle);
    if (err != DlpError::NONE)
        return err;

    PalmDatabase db;
    db.info = info;
    db.created = palm_seconds_from_date(info.created);
    db.modified = palm_seconds_from_date(info.modified);
    db.backed_up = palm_seconds_from_date(info.backed_up);
    err = read_contents(handle, db);
    DlpError close_err = _dlp.close_db(handle);
    if (err == DlpError::NONE)
        err = close_err;
    if (err != DlpError::NONE)
        return err;

    if (_storage.write_backup(hotsync_safe_name(_user.user_name), hotsync_backup_file_name(info), db.serialize()).is_error())
        return DlpError::SYSTEM;
    return DlpError::NONE;
}

DlpError HotSyncSession::read_contents(uint8_t handle, PalmDatabase &db)
{
    // A missing app or sort block is normal.
    DlpError err = _dlp.read_app_block(handle, db.app_info);
    if (err != DlpError::NONE && err != DlpError::NOT_FOUND)
        return err;
    err = _dlp.read_sort_block(handle, db.sort_info);
    if (err != DlpError::NONE && err != DlpError::NOT_FOUND)
        return err;

    uint16_t count = 0;
    err = _dlp.read_open_db_record_count(handle, count);
    if (err != DlpError::NONE)
        return err;

    for (uint16_t i = 0; i < count; ++i)
    {
        if (db.is_resource_db())
        {
            DlpResource resource;
            err = _dlp.read_resource_by_index(handle, i, resource);
            if (err != DlpError::NONE)
                return err;
            db.resources.push_back(std::move(resource));
        }
        else
        {
            DlpRecord record;
            err = _dlp.read_record_by_index(handle, i, record);
            if (err != DlpError::NONE)
                return err;
            db.records.push_back(std::move(record));
        }
    }
    return DlpError::NONE;
}

DlpError HotSyncSession::finish()
{
    uint8_t mod_flags = DLP_MOD_LAST_SYNC_PC;
    if (_user_is_new)
        mod_flags |= DLP_MOD_USER_ID | DLP_MOD_USER_NAME;
    _user.last_sync_pc = _options.pc_id;
    if (_options.now.year != 0)
    {
        _user.last_sync = _options.now;
        mod_flags |= DLP_MOD_LAST_SYNC_DATE;
    }
    DlpError err = _dlp.write_user_info(_user, mod_flags);
    if (err != DlpError::NONE)
        return err;

    log("FujiNet HotSync complete");
    if (_reset_after_sync)
        _dlp.reset_system();
    return _dlp.end_of_sync(0);
}

void HotSyncSession::log(const std::string &line)
{
    Debug_printf("HotSync: %s\r\n", line.c_str());
    _dlp.add_sync_log_entry(line + "\n");
}

// Per-database errors are skipped; a dead link or a user cancel ends the sync.
bool HotSyncSession::is_fatal(DlpError err) const
{
    return err == DlpError::TRANSPORT || err == DlpError::CANCEL_SYNC;
}
