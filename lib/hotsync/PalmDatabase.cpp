#include "PalmDatabase.h"

#include <algorithm>

struct PdbHeader {
    char name[32];
    u16be_t attributes;
    u16be_t version;
    u32be_t created;
    u32be_t modified;
    u32be_t backed_up;
    u32be_t mod_num;
    u32be_t app_info_offset;
    u32be_t sort_info_offset;
    char type[4];
    char creator[4];
    u32be_t unique_id_seed;
    u32be_t next_record_list;
    u16be_t num_records;
} __attribute__((packed));
static_assert(sizeof(PdbHeader) == 78, "PdbHeader must be 78 bytes");

struct PdbRecordEntry {
    u32be_t offset;
    uint8_t attributes;
    u24be_t unique_id;
} __attribute__((packed));
static_assert(sizeof(PdbRecordEntry) == 8, "PdbRecordEntry must be 8 bytes");

struct PrcResourceEntry {
    char type[4];
    u16be_t id;
    u32be_t offset;
} __attribute__((packed));
static_assert(sizeof(PrcResourceEntry) == 10, "PrcResourceEntry must be 10 bytes");

// Record attribute bits shared by the file format and DLP.
static constexpr uint8_t ATTR_DELETE = 0x80;
static constexpr uint8_t ATTR_BUSY = 0x20;
static constexpr uint8_t ATTR_ARCHIVE = 0x08;
static constexpr uint8_t ATTR_FLAGS_MASK = 0xF0;
static constexpr uint8_t ATTR_CATEGORY_MASK = 0x0F;
// Files conventionally carry two zero bytes between the entry list and data.
static constexpr size_t PDB_GAP = 2;

template <typename T>
static success_is_true read_struct(const ByteBuffer &file, size_t offset, T &out)
{
    if (offset + sizeof(T) > file.size())
        RETURN_ERROR_AS_FALSE();
    std::memcpy(&out, file.data() + offset, sizeof(T));
    RETURN_SUCCESS_AS_TRUE();
}

template <typename T>
static void append_struct(ByteBuffer &out, const T &value)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

// In a file, the low nibble is the category unless the record is deleted or
// busy, in which case bit 3 is the archive flag. DLP keeps them separate.
static void split_attributes(uint8_t file_attrs, uint8_t &dlp_attrs, uint8_t &category)
{
    dlp_attrs = file_attrs & ATTR_FLAGS_MASK;
    category = 0;
    if (file_attrs & (ATTR_DELETE | ATTR_BUSY))
        dlp_attrs |= file_attrs & ATTR_ARCHIVE;
    else
        category = file_attrs & ATTR_CATEGORY_MASK;
}

static uint8_t join_attributes(uint8_t dlp_attrs, uint8_t category)
{
    uint8_t file_attrs = dlp_attrs & ATTR_FLAGS_MASK;
    if (dlp_attrs & (ATTR_DELETE | ATTR_BUSY))
        return file_attrs | (dlp_attrs & ATTR_ARCHIVE);
    // Setting busy is the only way a file can keep an archived live record.
    if (dlp_attrs & ATTR_ARCHIVE)
        return file_attrs | ATTR_BUSY | ATTR_ARCHIVE;
    return file_attrs | (category & ATTR_CATEGORY_MASK);
}

success_is_true PalmDatabase::parse(const ByteBuffer &file)
{
    PdbHeader header;
    if (read_struct(file, 0, header).is_error())
        RETURN_ERROR_AS_FALSE();

    info = DlpDbInfo();
    info.name.assign(header.name, strnlen(header.name, sizeof(header.name)));
    info.flags = header.attributes;
    info.version = header.version;
    info.mod_num = header.mod_num;
    std::memcpy(info.type, header.type, 4);
    std::memcpy(info.creator, header.creator, 4);
    created = header.created;
    modified = header.modified;
    backed_up = header.backed_up;

    size_t count = header.num_records;
    size_t entry_size = is_resource_db() ? sizeof(PrcResourceEntry) : sizeof(PdbRecordEntry);
    std::vector<uint32_t> offsets;
    for (size_t i = 0; i < count; ++i)
    {
        size_t at = sizeof(header) + i * entry_size;
        if (is_resource_db())
        {
            PrcResourceEntry entry;
            if (read_struct(file, at, entry).is_error())
                RETURN_ERROR_AS_FALSE();
            DlpResource resource;
            std::memcpy(resource.type, entry.type, 4);
            resource.id = entry.id;
            resources.push_back(resource);
            offsets.push_back(entry.offset);
        }
        else
        {
            PdbRecordEntry entry;
            if (read_struct(file, at, entry).is_error())
                RETURN_ERROR_AS_FALSE();
            DlpRecord record;
            record.id = entry.unique_id;
            split_attributes(entry.attributes, record.attributes, record.category);
            records.push_back(record);
            offsets.push_back(entry.offset);
        }
    }

    // Each block runs to the start of the next one that follows it.
    auto block_end = [&](uint32_t start) {
        uint32_t end = file.size();
        for (uint32_t candidate : offsets)
            if (candidate > start && candidate < end)
                end = candidate;
        for (uint32_t candidate : {static_cast<uint32_t>(header.app_info_offset),
                                   static_cast<uint32_t>(header.sort_info_offset)})
            if (candidate > start && candidate < end)
                end = candidate;
        return end;
    };
    auto slice = [&](uint32_t start, ByteBuffer &out) {
        if (start == 0 || start > file.size())
            return;
        out.assign(file.begin() + start, file.begin() + block_end(start));
    };

    slice(header.app_info_offset, app_info);
    slice(header.sort_info_offset, sort_info);
    for (size_t i = 0; i < count; ++i)
    {
        if (offsets[i] > file.size())
            RETURN_ERROR_AS_FALSE();
        ByteBuffer &data = is_resource_db() ? resources[i].data : records[i].data;
        slice(offsets[i], data);
    }
    RETURN_SUCCESS_AS_TRUE();
}

ByteBuffer PalmDatabase::serialize() const
{
    size_t count = is_resource_db() ? resources.size() : records.size();
    size_t entry_size = is_resource_db() ? sizeof(PrcResourceEntry) : sizeof(PdbRecordEntry);
    uint32_t offset = sizeof(PdbHeader) + count * entry_size + PDB_GAP;

    PdbHeader header{};
    std::memcpy(header.name, info.name.data(), std::min(info.name.size(), sizeof(header.name) - 1));
    header.attributes = info.flags;
    header.version = info.version;
    header.created = created;
    header.modified = modified;
    header.backed_up = backed_up;
    header.mod_num = info.mod_num;
    header.app_info_offset = app_info.empty() ? 0 : offset;
    offset += app_info.size();
    header.sort_info_offset = sort_info.empty() ? 0 : offset;
    offset += sort_info.size();
    std::memcpy(header.type, info.type, 4);
    std::memcpy(header.creator, info.creator, 4);
    header.unique_id_seed = 0;
    header.next_record_list = 0;
    header.num_records = static_cast<uint16_t>(count);

    ByteBuffer out;
    append_struct(out, header);
    for (size_t i = 0; i < count; ++i)
    {
        if (is_resource_db())
        {
            PrcResourceEntry entry;
            std::memcpy(entry.type, resources[i].type, 4);
            entry.id = resources[i].id;
            entry.offset = offset;
            offset += resources[i].data.size();
            append_struct(out, entry);
        }
        else
        {
            PdbRecordEntry entry;
            entry.offset = offset;
            entry.attributes = join_attributes(records[i].attributes, records[i].category);
            entry.unique_id = records[i].id;
            offset += records[i].data.size();
            append_struct(out, entry);
        }
    }
    out.insert(out.end(), PDB_GAP, 0);
    out.insert(out.end(), app_info.begin(), app_info.end());
    out.insert(out.end(), sort_info.begin(), sort_info.end());
    for (size_t i = 0; i < count; ++i)
    {
        const ByteBuffer &data = is_resource_db() ? resources[i].data : records[i].data;
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

// Days from 0000-03-01 to 1904-01-01 in the proleptic Gregorian calendar.
static constexpr int32_t DAYS_0000_TO_1904 = 695361;

// Days between 1904-01-01 and the given civil date (Howard Hinnant's algorithm).
static int32_t days_since_1904(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int32_t>(doe) - DAYS_0000_TO_1904;
}

uint32_t palm_seconds_from_date(const DlpDateTime &t)
{
    if (t.year < 1904)
        return 0;
    uint32_t days = days_since_1904(t.year, t.month, t.day);
    return days * 86400u + t.hour * 3600u + t.minute * 60u + t.second;
}

DlpDateTime palm_date_from_seconds(uint32_t seconds)
{
    int32_t z = static_cast<int32_t>(seconds / 86400) + DAYS_0000_TO_1904;
    int32_t era = z / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;

    DlpDateTime t;
    t.day = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
    t.month = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
    t.year = static_cast<uint16_t>(static_cast<int32_t>(yoe) + era * 400 + (t.month <= 2));
    uint32_t rem = seconds % 86400;
    t.hour = rem / 3600;
    t.minute = (rem % 3600) / 60;
    t.second = rem % 60;
    return t;
}
