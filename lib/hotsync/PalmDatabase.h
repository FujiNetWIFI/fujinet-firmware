#ifndef HOTSYNC_PALM_DATABASE_H
#define HOTSYNC_PALM_DATABASE_H

// In-memory form of a Palm .pdb (record) or .prc (resource) file, converted
// to and from the shapes the DLP commands take.
// Reference: palm-pdb DatabaseHdrType, Palm File Format Specification

#include "DlpClient.h"

#include <vector>

struct PalmDatabase {
    DlpDbInfo info;
    uint32_t created = 0; // seconds since 1904-01-01
    uint32_t modified = 0;
    uint32_t backed_up = 0;
    ByteBuffer app_info;
    ByteBuffer sort_info;
    std::vector<DlpRecord> records;     // when info.flags lacks DLP_DB_RESOURCE
    std::vector<DlpResource> resources; // when info.flags has DLP_DB_RESOURCE

    bool is_resource_db() const { return info.flags & DLP_DB_RESOURCE; }

    success_is_true parse(const ByteBuffer &file);
    ByteBuffer serialize() const;
};

// Palm timestamps count seconds from 1904-01-01 with no time zone.
uint32_t palm_seconds_from_date(const DlpDateTime &t);
DlpDateTime palm_date_from_seconds(uint32_t seconds);

#endif // HOTSYNC_PALM_DATABASE_H
