#include "DlpClient.h"

#include "../../include/debug.h"

// Some operations (deleting a large database, installing a big resource) keep
// the device busy for a long while before it answers.
static constexpr uint32_t DLP_REPLY_WAIT_MS = 30000;
static constexpr uint16_t DLP_READ_WHOLE = 0xFFFF;
static constexpr uint16_t HOST_DLP_MAJOR = 1;
static constexpr uint16_t HOST_DLP_MINOR = 4;

static DlpDateTime read_date_time(DlpArgReader &reader)
{
    DlpDateTime t;
    t.year = reader.u16();
    t.month = reader.u8();
    t.day = reader.u8();
    t.hour = reader.u8();
    t.minute = reader.u8();
    t.second = reader.u8();
    reader.skip(1);
    return t;
}

static void write_date_time(DlpArgWriter &writer, const DlpDateTime &t)
{
    writer.u16(t.year).u8(t.month).u8(t.day).u8(t.hour).u8(t.minute).u8(t.second).u8(0);
}

static void read_type_id(DlpArgReader &reader, char out[5])
{
    ByteBuffer id = reader.bytes(4);
    std::memcpy(out, id.data(), 4);
    out[4] = '\0';
}

DlpError DlpClient::execute(const DlpRequest &request, DlpResponse &response)
{
    response = DlpResponse();
    if (_transport.send(request.encode()).is_error())
        return DlpError::TRANSPORT;

    ByteBuffer reply;
    if (_transport.receive(reply, DLP_REPLY_WAIT_MS).is_error())
        return DlpError::TRANSPORT;
    if (response.decode(request.func, reply).is_error())
    {
        Debug_printf("HotSync: malformed reply to DLP 0x%02x\r\n",
                     static_cast<unsigned>(request.func));
        return DlpError::TRANSPORT;
    }
    return response.error;
}

DlpError DlpClient::read_sys_info(DlpSysInfo &out)
{
    DlpRequest request(DlpFunc::ReadSysInfo);
    request.arg(DlpArgWriter().u16(HOST_DLP_MAJOR).u16(HOST_DLP_MINOR));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err != DlpError::NONE)
        return err;

    DlpArgReader reader(response.arg(0));
    out.rom_version = reader.u32();
    out.localization_id = reader.u32();
    // Only devices speaking DLP 1.2 or later return the second argument.
    DlpArgReader extended(response.arg(1));
    if (extended.remaining() >= 12)
    {
        out.dlp_major = extended.u16();
        out.dlp_minor = extended.u16();
        extended.skip(4);
        out.max_record_size = extended.u32();
    }
    return reader.failed() ? DlpError::TRANSPORT : DlpError::NONE;
}

DlpError DlpClient::read_user_info(DlpUserInfo &out)
{
    DlpResponse response;
    DlpError err = execute(DlpRequest(DlpFunc::ReadUserInfo), response);
    if (err != DlpError::NONE)
        return err;

    DlpArgReader reader(response.arg(0));
    out.user_id = reader.u32();
    out.viewer_id = reader.u32();
    out.last_sync_pc = reader.u32();
    out.successful_sync = read_date_time(reader);
    out.last_sync = read_date_time(reader);
    uint8_t name_length = reader.u8();
    reader.skip(1); // password length
    out.user_name = name_length > 0 ? reader.cstring() : std::string();
    return reader.failed() ? DlpError::TRANSPORT : DlpError::NONE;
}

DlpError DlpClient::write_user_info(const DlpUserInfo &info, uint8_t mod_flags)
{
    DlpArgWriter writer;
    writer.u32(info.user_id).u32(info.viewer_id).u32(info.last_sync_pc);
    write_date_time(writer, info.last_sync);
    writer.u8(mod_flags).u8(static_cast<uint8_t>(info.user_name.size() + 1)).cstring(info.user_name);

    DlpRequest request(DlpFunc::WriteUserInfo);
    request.arg(writer);
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::read_db_list(uint8_t flags, uint16_t start, std::vector<DlpDbInfo> &out,
                                 uint16_t &next_index)
{
    DlpRequest request(DlpFunc::ReadDBList);
    request.arg(DlpArgWriter().u8(flags).u8(0).u16(start));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err != DlpError::NONE)
        return err;

    DlpArgReader reader(response.arg(0));
    uint16_t last_index = reader.u16();
    reader.skip(1); // response flags
    uint8_t count = reader.u8();
    for (uint8_t i = 0; i < count && !reader.failed(); ++i)
    {
        uint8_t entry_size = reader.u8();
        ByteBuffer entry = reader.bytes(entry_size > 0 ? entry_size - 1 : 0);
        DlpArgReader fields(entry);
        DlpDbInfo info;
        info.misc_flags = fields.u8();
        info.flags = fields.u16();
        read_type_id(fields, info.type);
        read_type_id(fields, info.creator);
        info.version = fields.u16();
        info.mod_num = fields.u32();
        info.created = read_date_time(fields);
        info.modified = read_date_time(fields);
        info.backed_up = read_date_time(fields);
        info.index = fields.u16();
        info.name = fields.cstring();
        out.push_back(info);
    }
    next_index = last_index + 1;
    return reader.failed() ? DlpError::TRANSPORT : DlpError::NONE;
}

DlpError DlpClient::open_conduit()
{
    DlpResponse response;
    return execute(DlpRequest(DlpFunc::OpenConduit), response);
}

DlpError DlpClient::open_db(const std::string &name, uint8_t mode, uint8_t &db_handle)
{
    DlpRequest request(DlpFunc::OpenDB);
    request.arg(DlpArgWriter().u8(0).u8(mode).cstring(name));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err == DlpError::NONE)
        db_handle = DlpArgReader(response.arg(0)).u8();
    return err;
}

DlpError DlpClient::create_db(const DlpDbInfo &info, uint8_t &db_handle)
{
    DlpRequest request(DlpFunc::CreateDB);
    request.arg(DlpArgWriter()
                    .type_id(info.creator)
                    .type_id(info.type)
                    .u8(0)
                    .u8(0)
                    .u16(info.flags)
                    .u16(info.version)
                    .cstring(info.name));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err == DlpError::NONE)
        db_handle = DlpArgReader(response.arg(0)).u8();
    return err;
}

DlpError DlpClient::close_db(uint8_t db_handle)
{
    DlpRequest request(DlpFunc::CloseDB);
    request.arg(DlpArgWriter().u8(db_handle));
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::delete_db(const std::string &name)
{
    DlpRequest request(DlpFunc::DeleteDB);
    request.arg(DlpArgWriter().u8(0).u8(0).cstring(name));
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::read_open_db_record_count(uint8_t db_handle, uint16_t &count)
{
    DlpRequest request(DlpFunc::ReadOpenDBInfo);
    request.arg(DlpArgWriter().u8(db_handle));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err == DlpError::NONE)
        count = DlpArgReader(response.arg(0)).u16();
    return err;
}

DlpError DlpClient::read_block(DlpFunc func, uint8_t db_handle, ByteBuffer &out)
{
    DlpRequest request(func);
    request.arg(DlpArgWriter().u8(db_handle).u8(0).u16(0).u16(DLP_READ_WHOLE));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err != DlpError::NONE)
        return err;
    DlpArgReader reader(response.arg(0));
    reader.skip(2); // block size
    out = reader.rest();
    return DlpError::NONE;
}

DlpError DlpClient::write_block(DlpFunc func, uint8_t db_handle, const ByteBuffer &data)
{
    DlpRequest request(func);
    request.arg(DlpArgWriter().u8(db_handle).u8(0).u16(static_cast<uint16_t>(data.size())).bytes(data));
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::read_app_block(uint8_t db_handle, ByteBuffer &out)
{
    return read_block(DlpFunc::ReadAppBlock, db_handle, out);
}

DlpError DlpClient::read_sort_block(uint8_t db_handle, ByteBuffer &out)
{
    return read_block(DlpFunc::ReadSortBlock, db_handle, out);
}

DlpError DlpClient::write_app_block(uint8_t db_handle, const ByteBuffer &data)
{
    return write_block(DlpFunc::WriteAppBlock, db_handle, data);
}

DlpError DlpClient::write_sort_block(uint8_t db_handle, const ByteBuffer &data)
{
    return write_block(DlpFunc::WriteSortBlock, db_handle, data);
}

DlpError DlpClient::read_record_by_index(uint8_t db_handle, uint16_t index, DlpRecord &out)
{
    DlpRequest request(DlpFunc::ReadRecord);
    request.arg(DlpArgWriter().u8(db_handle).u8(0).u16(index).u16(0).u16(DLP_READ_WHOLE), 1);
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err != DlpError::NONE)
        return err;
    DlpArgReader reader(response.arg(0));
    out.id = reader.u32();
    reader.skip(4); // index, size
    out.attributes = reader.u8();
    out.category = reader.u8();
    out.data = reader.rest();
    return reader.failed() ? DlpError::TRANSPORT : DlpError::NONE;
}

DlpError DlpClient::write_record(uint8_t db_handle, const DlpRecord &record)
{
    static constexpr uint8_t WRITE_RECORD_FLAGS = 0x80;
    DlpRequest request(DlpFunc::WriteRecord);
    request.arg(DlpArgWriter()
                    .u8(db_handle)
                    .u8(WRITE_RECORD_FLAGS)
                    .u32(record.id)
                    .u8(record.attributes)
                    .u8(record.category)
                    .bytes(record.data));
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::read_resource_by_index(uint8_t db_handle, uint16_t index, DlpResource &out)
{
    DlpRequest request(DlpFunc::ReadResource);
    request.arg(DlpArgWriter().u8(db_handle).u8(0).u16(index).u16(0).u16(DLP_READ_WHOLE));
    DlpResponse response;
    DlpError err = execute(request, response);
    if (err != DlpError::NONE)
        return err;
    DlpArgReader reader(response.arg(0));
    read_type_id(reader, out.type);
    out.id = reader.u16();
    reader.skip(2); // index
    uint16_t size = reader.u16();
    out.data = reader.bytes(size);
    return reader.failed() ? DlpError::TRANSPORT : DlpError::NONE;
}

DlpError DlpClient::write_resource(uint8_t db_handle, const DlpResource &resource)
{
    DlpRequest request(DlpFunc::WriteResource);
    request.arg(DlpArgWriter()
                    .u8(db_handle)
                    .u8(0)
                    .type_id(resource.type)
                    .u16(resource.id)
                    .u16(static_cast<uint16_t>(resource.data.size()))
                    .bytes(resource.data));
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::add_sync_log_entry(const std::string &text)
{
    DlpRequest request(DlpFunc::AddSyncLogEntry);
    request.arg(DlpArgWriter().cstring(text));
    DlpResponse response;
    return execute(request, response);
}

DlpError DlpClient::reset_system()
{
    DlpResponse response;
    return execute(DlpRequest(DlpFunc::ResetSystem), response);
}

DlpError DlpClient::end_of_sync(uint16_t term_code)
{
    DlpRequest request(DlpFunc::EndOfSync);
    request.arg(DlpArgWriter().u16(term_code));
    DlpResponse response;
    return execute(request, response);
}
