#include "httpServiceApi.h"

#include "compat_string.h"

#include <cctype>
#include <cstdlib>
#include <utility>
#include <vector>

#include <cJSON.h>

#include "fnSystem.h"
#include "fnConfig.h"
#include "fnWiFi.h"
#include "fsFlash.h"
#include "fnFsSD.h"
#include "printer.h"
#include "fujiDevice.h"
#include "httpService.h"
#include "httpServiceBrowse.h"

#ifdef BUILD_ATARI
#include "sio/sioFuji.h"
#endif /* BUILD_ATARI */

#include "../../include/debug.h"

using namespace std;

namespace
{

// Method bitmask for the route table.
constexpr unsigned M_GET  = 1u << 0;
constexpr unsigned M_POST = 1u << 1;

// One parsed API request: whatever the route pattern captured, plus the body.
struct request
{
    int slot = -1;              // 1-based slot from the pattern's '*', -1 if absent/not a number
    fnHttpApi::method m = fnHttpApi::method::GET;
    const char *body = nullptr;
    size_t body_len = 0;
};

using handler_fn = fnHttpApi::response (*)(const request &);

struct route
{
    const char *pattern;  // '*' matches exactly one path segment and is captured
    unsigned methods;
    handler_fn fn;
};

// Build an {"error":"..."} response with the given status.
fnHttpApi::response json_error(const char *msg, int status)
{
    fnHttpApi::response r;
    r.status = status;
    r.body = "{\"error\":\"";
    r.body += msg;
    r.body += "\"}";
    return r;
}

// Print a cJSON tree, send it, and free everything.
fnHttpApi::response from_cjson(cJSON *root, int status = 200)
{
    if (root == nullptr)
        return json_error("out of memory", 500);

    char *json = cJSON_PrintUnformatted(root);
    fnHttpApi::response r;
    r.status = status;
    r.body = json;
    cJSON_free(json);
    cJSON_Delete(root);
    return r;
}

// Parse a JSON request body. Fills err and returns nullptr when the body is
// missing or not valid JSON.
cJSON *parse_body(const request &r, fnHttpApi::response &err)
{
    if (r.body == nullptr || r.body_len == 0)
    {
        err = json_error("failed to read request body", 400);
        return nullptr;
    }
    // The body is not NUL-terminated (mongoose in particular hands over a
    // pointer into the request buffer), so length-limited parsing is required.
    cJSON *body = cJSON_ParseWithLength(r.body, r.body_len);
    if (body == nullptr)
        err = json_error("invalid JSON", 400);
    return body;
}

// GET /api/v1/status - System status information
fnHttpApi::response h_status(const request &)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr)
        return json_error("out of memory", 500);

    cJSON_AddStringToObject(root, "version", fnSystem.get_fujinet_version(true));
    cJSON_AddStringToObject(root, "ip", fnSystem.Net.get_ip4_address_str().c_str());
    cJSON_AddStringToObject(root, "hostname", fnSystem.Net.get_hostname().c_str());
    cJSON_AddNumberToObject(root, "uptime_s", fnSystem.get_uptime() / 1000000);
    cJSON_AddNumberToObject(root, "free_heap", fnSystem.get_free_heap_size());
    cJSON_AddNumberToObject(root, "cpu_freq_mhz", fnSystem.get_cpu_frequency());
    cJSON_AddStringToObject(root, "sdk_version", fnSystem.get_sdk_version());
    // What the REST handlers do, not just that they are here: 1 was the
    // original host and mount handlers, which wrote the config file and left
    // the live slots alone; 2 acts on the live slots.
    cJSON_AddNumberToObject(root, "api_version", 2);

    // WiFi info
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi)
    {
        cJSON_AddBoolToObject(wifi, "connected", fnWiFi.connected());
        cJSON_AddStringToObject(wifi, "ssid", fnWiFi.get_current_ssid().c_str());
        cJSON_AddStringToObject(wifi, "ip", fnSystem.Net.get_ip4_address_str().c_str());
    }

#ifdef ESP_PLATFORM
    cJSON_AddBoolToObject(root, "sd_present", fsFlash.exists("/"));
#else
    // On PC there's no separate flash filesystem to check for presence, so use
    // whether the SD card filesystem is actually mounted/running instead of the
    // ESP behavior (fsFlash.exists("/")), which would always report true here.
    cJSON_AddBoolToObject(root, "sd_present", fnSDFAT.running());
#endif

    return from_cjson(root);
}

// GET /api/v1/drives - List all drive slots
fnHttpApi::response h_drives(const request &)
{
    cJSON *root = cJSON_CreateArray();
    if (root == nullptr)
        return json_error("out of memory", 500);

    for (int i = 0; i < MAX_MOUNT_SLOTS; i++)
    {
        cJSON *slot = cJSON_CreateObject();
        if (slot == nullptr)
            continue;

        cJSON_AddNumberToObject(slot, "slot", i + 1);

        std::string path = Config.get_mount_path(i);
        cJSON_AddStringToObject(slot, "path", path.c_str());
        cJSON_AddBoolToObject(slot, "mounted", !path.empty());

        fnConfig::mount_mode_t mode = Config.get_mount_mode(i);
        cJSON_AddStringToObject(slot, "mode", mode == fnConfig::MOUNTMODE_WRITE ? "w" : "r");

        int host_slot = Config.get_mount_host_slot(i);
        cJSON_AddNumberToObject(slot, "host_slot", host_slot);

        if (host_slot >= 0 && host_slot < MAX_HOST_SLOTS)
        {
            cJSON_AddStringToObject(slot, "host", Config.get_host_name(host_slot).c_str());
        }

        cJSON_AddItemToArray(root, slot);
    }

    return from_cjson(root);
}

// GET /api/v1/drives/{slot} - Get specific drive slot
fnHttpApi::response h_drive_slot(const request &r)
{
    int slot = r.slot;
    if (slot < 1 || slot > MAX_MOUNT_SLOTS)
        return json_error("invalid slot", 400);
    slot--; // Convert to 0-based

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr)
        return json_error("out of memory", 500);

    cJSON_AddNumberToObject(root, "slot", slot + 1);

    std::string path = Config.get_mount_path(slot);
    cJSON_AddStringToObject(root, "path", path.c_str());
    cJSON_AddBoolToObject(root, "mounted", !path.empty());

    fnConfig::mount_mode_t mode = Config.get_mount_mode(slot);
    cJSON_AddStringToObject(root, "mode", mode == fnConfig::MOUNTMODE_WRITE ? "w" : "r");

    int host_slot = Config.get_mount_host_slot(slot);
    cJSON_AddNumberToObject(root, "host_slot", host_slot);

    if (host_slot >= 0 && host_slot < MAX_HOST_SLOTS)
    {
        cJSON_AddStringToObject(root, "host", Config.get_host_name(host_slot).c_str());
    }

    return from_cjson(root);
}

// POST /api/v1/drives/{slot}/mount - Mount a disk image
fnHttpApi::response h_drive_mount(const request &r)
{
    int slot = r.slot;
    if (slot < 1 || slot > MAX_MOUNT_SLOTS)
        return json_error("invalid slot", 400);
    slot--; // Convert to 0-based

    fnHttpApi::response err;
    cJSON *body = parse_body(r, err);
    if (body == nullptr)
        return err;

    cJSON *path_json = cJSON_GetObjectItem(body, "path");
    cJSON *host_slot_json = cJSON_GetObjectItem(body, "host_slot");
    cJSON *mode_json = cJSON_GetObjectItem(body, "mode");

    if (path_json == nullptr || host_slot_json == nullptr)
    {
        cJSON_Delete(body);
        return json_error("missing path or host_slot", 400);
    }

    const char *path = cJSON_GetStringValue(path_json);
    int host_slot = cJSON_GetNumberValue(host_slot_json);
    const char *mode_str = mode_json ? cJSON_GetStringValue(mode_json) : "r";

    if (path == nullptr || host_slot < 0 || host_slot >= MAX_HOST_SLOTS)
    {
        cJSON_Delete(body);
        return json_error("invalid parameters", 400);
    }

    fnConfig::mount_mode_t mount_mode = (strcmp(mode_str, "w") == 0)
        ? fnConfig::MOUNTMODE_WRITE
        : fnConfig::MOUNTMODE_READ;

    // Recording the mount is not mounting it: the host has to be up and the
    // image opened before the slot is live. Same sequence the web UI runs.
    if (!theFuji->get_host(host_slot)->mount())
    {
        cJSON_Delete(body);
        return json_error("could not mount host slot", 500);
    }

    fujiDisk *disk = theFuji->get_disk(slot);
    // Remember what the slot held. An image that fails to open must not be
    // left sitting in the live slot, where the next config sync would write it
    // out as a mount that never happened.
    uint8_t prev_host_slot = disk->host_slot;
    disk_access_flags_t prev_access_mode = disk->access_mode;
    char prev_filename[sizeof(disk->filename)];
    strlcpy(prev_filename, disk->filename, sizeof(prev_filename));

    disk->host_slot = host_slot;
    strlcpy(disk->filename, path, sizeof(disk->filename));

    disk_access_flags_t access_mode = (mount_mode == fnConfig::MOUNTMODE_WRITE)
        ? DISK_ACCESS_MODE_WRITE
        : DISK_ACCESS_MODE_READ;

    if (!theFuji->fujicore_mount_disk_image_success(slot, access_mode))
    {
        disk->host_slot = prev_host_slot;
        disk->access_mode = prev_access_mode;
        strlcpy(disk->filename, prev_filename, sizeof(disk->filename));
        cJSON_Delete(body);
        return json_error("could not mount disk image", 500);
    }

    Config.store_mount(slot, host_slot, path, mount_mode);
    Config.save();
    theFuji->populate_slots_from_config();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "success");
    cJSON_AddNumberToObject(resp, "slot", slot + 1);
    cJSON_AddStringToObject(resp, "path", path);

    fnHttpApi::response ok = from_cjson(resp);
    cJSON_Delete(body);
    return ok;
}

// POST /api/v1/drives/{slot}/eject - Eject disk from slot
fnHttpApi::response h_drive_eject(const request &r)
{
    int slot = r.slot;
    if (slot < 1 || slot > MAX_MOUNT_SLOTS)
        return json_error("invalid slot", 400);
    slot--; // Convert to 0-based

    fnHttpBrowse::eject_slot(slot);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "success");
    cJSON_AddNumberToObject(resp, "slot", slot + 1);

    return from_cjson(resp);
}

// GET /api/v1/hosts - List all host slots
fnHttpApi::response h_hosts(const request &)
{
    cJSON *root = cJSON_CreateArray();
    if (root == nullptr)
        return json_error("out of memory", 500);

    for (int i = 0; i < MAX_HOST_SLOTS; i++)
    {
        cJSON *slot = cJSON_CreateObject();
        if (slot == nullptr)
            continue;

        cJSON_AddNumberToObject(slot, "slot", i + 1);

        // Hostname comes from the live slot because that is what the rest of
        // the firmware runs on; type still has to come from the config.
        std::string name = theFuji->get_host(i)->get_hostname();
        cJSON_AddStringToObject(slot, "hostname", name.c_str());
        cJSON_AddBoolToObject(slot, "configured", !name.empty());

        fnConfig::host_type_t type = Config.get_host_type(i);
        cJSON_AddStringToObject(slot, "type",
            type == fnConfig::HOSTTYPE_SD ? "SD" :
            type == fnConfig::HOSTTYPE_TNFS ? "TNFS" : "unknown");

        cJSON_AddItemToArray(root, slot);
    }

    return from_cjson(root);
}

// GET/POST /api/v1/hosts/{slot} - Get or update host slot
fnHttpApi::response h_host_slot(const request &r)
{
    int slot = r.slot;
    if (slot < 1 || slot > MAX_HOST_SLOTS)
        return json_error("invalid slot", 400);
    slot--; // Convert to 0-based

    if (r.m == fnHttpApi::method::GET)
    {
        cJSON *root = cJSON_CreateObject();
        if (root == nullptr)
            return json_error("out of memory", 500);

        cJSON_AddNumberToObject(root, "slot", slot + 1);

        // Hostname comes from the live slot because that is what the rest of
        // the firmware runs on; type still has to come from the config.
        std::string name = theFuji->get_host(slot)->get_hostname();
        cJSON_AddStringToObject(root, "hostname", name.c_str());
        cJSON_AddBoolToObject(root, "configured", !name.empty());

        fnConfig::host_type_t type = Config.get_host_type(slot);
        cJSON_AddStringToObject(root, "type",
            type == fnConfig::HOSTTYPE_SD ? "SD" :
            type == fnConfig::HOSTTYPE_TNFS ? "TNFS" : "unknown");

        return from_cjson(root);
    }
    else if (r.m == fnHttpApi::method::POST)
    {
        fnHttpApi::response err;
        cJSON *body = parse_body(r, err);
        if (body == nullptr)
            return err;

        cJSON *hostname_json = cJSON_GetObjectItem(body, "hostname");
        cJSON *type_json = cJSON_GetObjectItem(body, "type");

        if (hostname_json == nullptr)
        {
            cJSON_Delete(body);
            return json_error("missing hostname", 400);
        }

        const char *hostname = cJSON_GetStringValue(hostname_json);
        if (hostname == nullptr)
        {
            cJSON_Delete(body);
            return json_error("invalid hostname", 400);
        }

        fnConfig::host_type_t host_type = fnConfig::HOSTTYPE_SD;
        if (type_json != nullptr)
        {
            const char *type_str = cJSON_GetStringValue(type_json);
            if (type_str != nullptr)
            {
                if (strcasecmp(type_str, "TNFS") == 0)
                    host_type = fnConfig::HOSTTYPE_TNFS;
                else
                    host_type = fnConfig::HOSTTYPE_SD;
            }
        }

        // Config alone is not enough: the web UI, the drive chooser and the bus
        // all read theFuji's live slots, and set_slot_hostname() syncs the
        // config back from them.
        theFuji->set_slot_hostname(slot, (char *)hostname);
        // populate_config_from_slots() cannot know the type the caller asked
        // for — the live host works the protocol out from the name when it
        // mounts — so put an explicitly requested one back afterwards.
        if (type_json != nullptr && hostname[0] != '\0')
            Config.store_host(slot, hostname, host_type);
        Config.save();

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "success");
        cJSON_AddNumberToObject(resp, "slot", slot + 1);
        cJSON_AddStringToObject(resp, "hostname", hostname);

        fnHttpApi::response ok = from_cjson(resp);
        cJSON_Delete(body);
        return ok;
    }

    return json_error("method not allowed", 405);
}

// GET /api/v1/printer/status - Printer status
fnHttpApi::response h_printer_status(const request &)
{
    PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);
    if (printer == nullptr)
    {
        fnHttpApi::response resp;
        resp.body = "{\"enabled\":false}";
        return resp;
    }

    printer_emu *emu = printer->getPrinterPtr();
    if (emu == nullptr)
    {
        fnHttpApi::response resp;
        resp.body = "{\"enabled\":false}";
        return resp;
    }

    uint64_t now = fnSystem.millis();
    bool ready = (now - printer->lastPrintTime() >= PRINTER_BUSY_TIME);
    size_t sz = emu->getOutputSize();

    const char *ct;
    switch (emu->getPaperType())
    {
    case RAW:
    case TRIM:
    case ASCII:
        ct = "text/plain";
        break;
    case PDF:
        ct = "application/pdf";
        break;
    case SVG:
        ct = "image/svg+xml";
        break;
    case PNG:
        ct = "image/png";
        break;
    case HTML:
    case HTML_ATASCII:
        ct = "text/html";
        break;
    default:
        ct = "application/octet-stream";
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", true);
    cJSON_AddStringToObject(root, "model", emu->modelname());
    cJSON_AddBoolToObject(root, "ready", ready);
    cJSON_AddBoolToObject(root, "has_output", sz > 0);
    cJSON_AddNumberToObject(root, "output_size", sz);
    cJSON_AddStringToObject(root, "content_type", ct);
    cJSON_AddNumberToObject(root, "last_print_time", printer->lastPrintTime());

    return from_cjson(root);
}

// POST /api/v1/printer/clear - Clear printer output
fnHttpApi::response h_printer_clear(const request &)
{
    PRINTER_CLASS *printer = (PRINTER_CLASS *)fnPrinters.get_ptr(0);
    if (printer == nullptr || printer->getPrinterPtr() == nullptr)
        return json_error("printer not available", 400);

    printer_emu *emu = printer->getPrinterPtr();
    emu->closeOutput();
    printer->reset_printer();

    // Try to remove the file (may fail on some filesystems)
    int remove_result = fsFlash.remove("/paper");
    Debug_printf("Attempting to remove /paper, result: %d\n", remove_result);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "success");

    return from_cjson(resp);
}

// POST /api/v1/wifi/scan - Scan WiFi networks
fnHttpApi::response h_wifi_scan(const request &)
{
    uint8_t count = fnWiFi.scan_networks();
    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < count; i++)
    {
        char ssid[33] = {0};
        uint8_t rssi = 0;
        uint8_t channel = 0;
        char bssid[18] = {0};
        uint8_t encryption = 0;

        fnWiFi.get_scan_result(i, ssid, &rssi, &channel, bssid, &encryption);

        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", ssid);
        cJSON_AddNumberToObject(network, "rssi", rssi);
        cJSON_AddNumberToObject(network, "channel", channel);
        cJSON_AddStringToObject(network, "bssid", bssid);
        cJSON_AddNumberToObject(network, "encryption", encryption);
        cJSON_AddItemToArray(root, network);
    }

    return from_cjson(root);
}

// GET /api/v1/wifi/status - Current WiFi connection info
fnHttpApi::response h_wifi_status(const request &)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr)
        return json_error("out of memory", 500);

    cJSON_AddBoolToObject(root, "connected", fnWiFi.connected());
    cJSON_AddStringToObject(root, "ssid", fnWiFi.get_current_ssid().c_str());
    cJSON_AddStringToObject(root, "detail", fnWiFi.get_current_detail_str());

    uint8_t bssid[6] = {0};
    fnWiFi.get_current_bssid(bssid);
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    cJSON_AddStringToObject(root, "bssid", bssid_str);

    cJSON_AddStringToObject(root, "ip", fnSystem.Net.get_ip4_address_str().c_str());
    cJSON_AddStringToObject(root, "gateway", fnSystem.Net.get_ip4_gateway_str().c_str());
    cJSON_AddStringToObject(root, "dns", fnSystem.Net.get_ip4_dns_str().c_str());

    char mac_str[18];
    uint8_t mac[6];
    fnWiFi.get_mac(mac);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);

    return from_cjson(root);
}

// Route table. Order only matters for readability; matching is by pattern.
const route routes[] = {
    {FN_API_ROOT "/status",         M_GET,            h_status},
    {FN_API_ROOT "/drives",         M_GET,            h_drives},
    {FN_API_ROOT "/drives/*",       M_GET,            h_drive_slot},
    {FN_API_ROOT "/drives/*/mount", M_POST,           h_drive_mount},
    {FN_API_ROOT "/drives/*/eject", M_POST,           h_drive_eject},
    {FN_API_ROOT "/hosts",          M_GET,            h_hosts},
    {FN_API_ROOT "/hosts/*",        M_GET | M_POST,   h_host_slot},
    {FN_API_ROOT "/printer/status", M_GET,            h_printer_status},
    {FN_API_ROOT "/printer/clear",  M_POST,           h_printer_clear},
    {FN_API_ROOT "/wifi/scan",      M_POST,           h_wifi_scan},
    {FN_API_ROOT "/wifi/status",    M_GET,            h_wifi_status},
};

// Split a '/'-delimited string into its segments (an empty leading segment
// from the leading '/' is included, so both sides have it and it cancels out).
void split_segments(const char *s, size_t len, std::vector<std::pair<size_t, size_t>> &segs)
{
    size_t start = 0;
    for (size_t i = 0; i <= len; i++)
    {
        if (i == len || s[i] == '/')
        {
            segs.emplace_back(start, i - start);
            start = i + 1;
        }
    }
}

// Match path against pattern segment by segment. A '*' segment in the
// pattern matches any one non-empty segment of path and is captured.
bool match_pattern(const char *pattern, const std::string &path, std::string &capture)
{
    std::vector<std::pair<size_t, size_t>> psegs, ssegs;
    split_segments(pattern, strlen(pattern), psegs);
    split_segments(path.c_str(), path.size(), ssegs);

    if (psegs.size() != ssegs.size())
        return false;

    for (size_t i = 0; i < psegs.size(); i++)
    {
        size_t pstart = psegs[i].first, plen = psegs[i].second;
        size_t sstart = ssegs[i].first, slen = ssegs[i].second;

        if (plen == 1 && pattern[pstart] == '*')
        {
            if (slen == 0)
                return false; // '*' requires a non-empty segment
            capture.assign(path, sstart, slen);
        }
        else
        {
            if (plen != slen || strncmp(pattern + pstart, path.c_str() + sstart, plen) != 0)
                return false;
        }
    }

    return true;
}

} // namespace

fnHttpApi::response fnHttpApi::handle(const std::string &uri, method m, const char *body, size_t body_len)
{
    // A server may leave a query string on; strip it.
    std::string path = uri.substr(0, uri.find('?'));

    // Normalize a trailing slash so /api/v1/drives/ behaves like /api/v1/drives.
    if (path.size() > 1 && path.back() == '/')
        path.pop_back();

    bool path_matched = false;

    for (const route &rt : routes)
    {
        std::string capture;
        if (!match_pattern(rt.pattern, path, capture))
            continue;

        path_matched = true;

        unsigned bit = (m == method::GET) ? M_GET : (m == method::POST) ? M_POST : 0;
        if ((rt.methods & bit) == 0)
            continue;

        request r;
        r.m = m;
        r.body = body;
        r.body_len = body_len;

        if (!capture.empty() && capture.size() <= 3)
        {
            bool digits_only = true;
            for (char ch : capture)
                if (!isdigit((unsigned char)ch))
                {
                    digits_only = false;
                    break;
                }
            if (digits_only)
                r.slot = atoi(capture.c_str());
        }

        return rt.fn(r);
    }

    if (path_matched)
        return json_error("method not allowed", 405);
    return json_error("not found", 404);
}
