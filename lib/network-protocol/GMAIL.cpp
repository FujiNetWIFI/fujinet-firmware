/**
 * NetworkProtocolGMAIL
 *
 * Gmail mailbox adapter for FujiNet. Reuses the Google Drive OAuth grant and
 * the same HTTP/JSON/base64 helpers as GDRIVE, talking to the Gmail REST API.
 */

#include "GMAIL.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#else
#include "../http/mgHttpClient.h"
#endif

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "../encoding/base64.h"
#include "status_error_codes.h"

// Token refresh goes through the same relay as Google Drive; the shared grant
// must carry the gmail.readonly scope for the returned token to reach Gmail.
#define GMAIL_RELAY_REFRESH_URL "https://auth.fujinet.online/gdrive-refresh"

// ─── file-local helpers ───────────────────────────────────────────────────────

namespace {

bool ci_equal(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\"");
    size_t b = s.find_last_not_of(" \t\"");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

// Split an RFC5322 From header into display name and email address.
void parse_from(const std::string &from, std::string &name, std::string &email)
{
    size_t lt = from.find('<');
    if (lt != std::string::npos)
    {
        size_t gt = from.find('>', lt);
        email = trim(from.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1));
        name = trim(from.substr(0, lt));
    }
    else
    {
        email = trim(from);
        name = "";
    }
}

std::string decode_b64url(const std::string &data)
{
    if (data.empty()) return "";
    size_t out_len = 0;
    auto p = Base64::url_decode(data.c_str(), data.size(), &out_len);
    if (!p) return "";
    return std::string((char *)p.get(), out_len);
}

} // namespace

// ─── construction ─────────────────────────────────────────────────────────────

NetworkProtocolGMAIL::NetworkProtocolGMAIL(std::string *rx_buf, std::string *tx_buf,
                                           std::string *sp_buf)
    : NetworkProtocolMailbox(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolGMAIL::ctor\r\n");
}

NetworkProtocolGMAIL::~NetworkProtocolGMAIL()
{
    Debug_printf("NetworkProtocolGMAIL::dtor\r\n");
}

// ─── static helpers (copied from GDRIVE) ──────────────────────────────────────

std::string NetworkProtocolGMAIL::url_encode(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string NetworkProtocolGMAIL::json_str(cJSON *obj, const char *key)
{
    if (!obj) return "";
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item)) return "";
    return item->valuestring ? item->valuestring : "";
}

// ─── OAuth2 token management (reuses GDRIVE token storage / relay) ─────────────

bool NetworkProtocolGMAIL::refresh_access_token()
{
    std::string refresh_token = Config.get_gdrive_refresh_token();
    if (refresh_token.empty())
    {
        Debug_printf("GMAIL: no refresh token — user must re-authorize\r\n");
        return false;
    }

    std::string body = "refresh_token=" + url_encode(refresh_token);
    std::string resp = api_post(GMAIL_RELAY_REFRESH_URL, body, "application/x-www-form-urlencoded");
    if (resp.empty()) return false;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return false;

    std::string token = json_str(j, "access_token");
    cJSON *expires_in = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    long ttl = (expires_in && cJSON_IsNumber(expires_in)) ? (long)expires_in->valuedouble : 3600;
    cJSON_Delete(j);

    if (token.empty()) return false;

    Config.store_gdrive_access_token(token);
    Config.store_gdrive_token_expiry((long)time(nullptr) + ttl);
    Config.save();

    _access_token = token;
    Debug_printf("GMAIL: access token refreshed, expires in %ld s\r\n", ttl);
    return true;
}

bool NetworkProtocolGMAIL::ensure_access_token()
{
    long now = (long)time(nullptr);
    if (now >= Config.get_gdrive_token_expiry() - 60)
        return refresh_access_token();
    _access_token = Config.get_gdrive_access_token();
    return !_access_token.empty();
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────

#ifdef ESP_PLATFORM

std::string NetworkProtocolGMAIL::api_get(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 15000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { _last_http = 0; return ""; }

    std::string auth = "Bearer " + _access_token;
    esp_http_client_set_header(h, "Authorization", auth.c_str());

    if (esp_http_client_open(h, 0) != ESP_OK)
    {
        esp_http_client_cleanup(h);
        _last_http = 0;
        return "";
    }
    esp_http_client_fetch_headers(h);

    _last_http = esp_http_client_get_status_code(h);

    std::string body;
    char buf[512];
    int n;
    while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0)
        body.append(buf, n);

    esp_http_client_close(h);
    esp_http_client_cleanup(h);

    if (_last_http < 200 || _last_http >= 300)
    {
        Debug_printf("GMAIL api_get: HTTP %d for %s: %s\r\n", _last_http, url.c_str(), body.c_str());
        return "";
    }
    return body;
}

std::string NetworkProtocolGMAIL::api_post(const std::string &url, const std::string &body,
                                           const std::string &content_type)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 15000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { _last_http = 0; return ""; }

    esp_http_client_set_method(h, HTTP_METHOD_POST);
    if (!_access_token.empty())
        esp_http_client_set_header(h, "Authorization", ("Bearer " + _access_token).c_str());
    esp_http_client_set_header(h, "Content-Type", content_type.c_str());

    int wlen = (int)body.size();
    if (esp_http_client_open(h, wlen) != ESP_OK)
    {
        esp_http_client_cleanup(h);
        _last_http = 0;
        return "";
    }
    esp_http_client_write(h, body.c_str(), wlen);
    esp_http_client_fetch_headers(h);

    _last_http = esp_http_client_get_status_code(h);
    if (_last_http < 200 || _last_http >= 300)
    {
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
        return "";
    }

    std::string resp;
    char buf[512];
    int n;
    while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0)
        resp.append(buf, n);

    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return resp;
}

#else /* !ESP_PLATFORM */

std::string NetworkProtocolGMAIL::api_get(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) { _last_http = 0; return ""; }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    _last_http = http.GET();
    std::string body;
    uint8_t buf[512];
    int n;
    while ((n = http.read(buf, sizeof(buf))) > 0)
        body.append((char *)buf, n);
    if (_last_http < 200 || _last_http >= 300)
    {
        Debug_printf("GMAIL api_get: HTTP %d for %s: %s\r\n", _last_http, url.c_str(), body.c_str());
        return "";
    }
    return body;
}

std::string NetworkProtocolGMAIL::api_post(const std::string &url, const std::string &body,
                                           const std::string &content_type)
{
    mgHttpClient http;
    if (!http.begin(url)) { _last_http = 0; return ""; }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    http.set_header("Content-Type", content_type.c_str());
    _last_http = http.POST(body.c_str(), (int)body.size());
    if (_last_http < 200 || _last_http >= 300)
        return "";
    std::string resp;
    uint8_t buf[512];
    int n;
    while ((n = http.read(buf, sizeof(buf))) > 0)
        resp.append((char *)buf, n);
    return resp;
}

#endif /* ESP_PLATFORM */

// ─── Gmail helpers ────────────────────────────────────────────────────────────

std::string NetworkProtocolGMAIL::label_id_for(const std::string &folder, uint32_t *total)
{
    std::string resp = api_get(std::string(GMAIL_BASE) + "/labels");
    if (resp.empty()) return "";

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return "";

    std::string id;
    cJSON *labels = cJSON_GetObjectItemCaseSensitive(j, "labels");
    if (cJSON_IsArray(labels))
    {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, labels)
        {
            if (ci_equal(json_str(item, "name"), folder))
            {
                id = json_str(item, "id");
                break;
            }
        }
    }
    cJSON_Delete(j);

    if (id.empty())
    {
        _last_http = 404;
        return "";
    }

    if (total)
    {
        *total = 0;
        std::string r2 = api_get(std::string(GMAIL_BASE) + "/labels/" + url_encode(id));
        if (!r2.empty())
        {
            cJSON *j2 = cJSON_Parse(r2.c_str());
            cJSON *mt = j2 ? cJSON_GetObjectItemCaseSensitive(j2, "messagesTotal") : nullptr;
            if (mt && cJSON_IsNumber(mt)) *total = (uint32_t)mt->valuedouble;
            cJSON_Delete(j2);
        }
    }
    return id;
}

std::vector<std::string> NetworkProtocolGMAIL::list_message_ids(const std::string &labelId,
                                                               size_t needed)
{
    std::vector<std::string> ids;
    std::string pageToken;

    while (ids.size() < needed)
    {
        size_t want = needed - ids.size();
        if (want > 500) want = 500;

        std::string url = std::string(GMAIL_BASE) + "/messages?labelIds=" + url_encode(labelId) +
                          "&maxResults=" + std::to_string(want);
        if (!pageToken.empty())
            url += "&pageToken=" + url_encode(pageToken);

        std::string resp = api_get(url);
        if (resp.empty()) break;

        cJSON *j = cJSON_Parse(resp.c_str());
        if (!j) break;

        cJSON *msgs = cJSON_GetObjectItemCaseSensitive(j, "messages");
        if (cJSON_IsArray(msgs))
        {
            cJSON *m = nullptr;
            cJSON_ArrayForEach(m, msgs)
                ids.push_back(json_str(m, "id"));
        }
        pageToken = json_str(j, "nextPageToken");
        cJSON_Delete(j);

        if (pageToken.empty()) break;
    }
    return ids;
}

std::string NetworkProtocolGMAIL::message_id_for_seq(const std::string &folder, uint32_t seq,
                                                     bool &ok)
{
    ok = false;
    uint32_t total = 0;
    std::string labelId = label_id_for(folder, &total);
    if (labelId.empty() || seq < 1 || seq > total)
        return "";

    long i = (long)total - (long)seq; // newest-first index (0 = newest)
    std::vector<std::string> ids = list_message_ids(labelId, (size_t)i + 1);
    if ((long)ids.size() <= i)
        return "";

    ok = true;
    return ids[i];
}

cJSON *NetworkProtocolGMAIL::get_message_full(const std::string &id)
{
    std::string resp = api_get(std::string(GMAIL_BASE) + "/messages/" + id + "?format=full");
    if (resp.empty()) return nullptr;
    return cJSON_Parse(resp.c_str());
}

// Recursive search for the first leaf part whose mimeType starts with wantMime.
static bool find_text_part(cJSON *payload, const char *wantMime, std::string &out)
{
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(payload, "parts");
    if (cJSON_IsArray(parts))
    {
        cJSON *p = nullptr;
        cJSON_ArrayForEach(p, parts)
            if (find_text_part(p, wantMime, out)) return true;
        return false;
    }
    cJSON *mimeType = cJSON_GetObjectItemCaseSensitive(payload, "mimeType");
    std::string mt = (mimeType && cJSON_IsString(mimeType) && mimeType->valuestring)
                         ? mimeType->valuestring : "";
    if (mt.rfind(wantMime, 0) == 0)
    {
        cJSON *body = cJSON_GetObjectItemCaseSensitive(payload, "body");
        cJSON *data = body ? cJSON_GetObjectItemCaseSensitive(body, "data") : nullptr;
        if (data && cJSON_IsString(data) && data->valuestring)
        {
            out = decode_b64url(data->valuestring);
            return true;
        }
    }
    return false;
}

bool NetworkProtocolGMAIL::extract_body(cJSON *payload, std::string &out)
{
    if (!payload) return false;
    if (find_text_part(payload, "text/plain", out)) return true;
    return find_text_part(payload, "text/html", out);
}

void NetworkProtocolGMAIL::collect_attachments(cJSON *payload, std::vector<cJSON *> &out)
{
    if (!payload) return;
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(payload, "parts");
    if (cJSON_IsArray(parts))
    {
        cJSON *p = nullptr;
        cJSON_ArrayForEach(p, parts)
            collect_attachments(p, out);
        return;
    }
    if (!json_str(payload, "filename").empty())
        out.push_back(payload);
}

// ─── mailbox provider hooks ───────────────────────────────────────────────────

fujiError_t NetworkProtocolGMAIL::connect_and_auth()
{
    if (!ensure_access_token())
    {
        _last_http = 401;
        return FUJI_ERROR::UNSPECIFIED;
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGMAIL::folder_count(const std::string &folder, uint32_t &count)
{
    uint32_t total = 0;
    if (label_id_for(folder, &total).empty())
        return FUJI_ERROR::UNSPECIFIED;
    count = total;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGMAIL::folder_index(const std::string &folder, long rangeStart,
                                               long rangeEnd, bool newest,
                                               std::vector<MailboxIndexEntry> &out)
{
    uint32_t total = 0;
    std::string labelId = label_id_for(folder, &total);
    if (labelId.empty())
        return FUJI_ERROR::UNSPECIFIED;
    if (total == 0)
        return FUJI_ERROR::NONE;

    long maxIdx = (long)total - 1;
    if (rangeStart > maxIdx)
        return FUJI_ERROR::NONE;
    if (rangeEnd > maxIdx)
        rangeEnd = maxIdx;

    std::vector<std::string> ids = list_message_ids(labelId, (size_t)rangeEnd + 1);

    for (long i = rangeStart; i <= rangeEnd && i < (long)ids.size(); i++)
    {
        std::string url = std::string(GMAIL_BASE) + "/messages/" + ids[i] +
                          "?format=metadata&metadataHeaders=From&metadataHeaders=Subject"
                          "&metadataHeaders=Date";
        std::string resp = api_get(url);
        if (resp.empty()) continue;

        cJSON *j = cJSON_Parse(resp.c_str());
        if (!j) continue;

        MailboxIndexEntry e;
        e.msgNum = total - (uint32_t)i;

        std::string internal = json_str(j, "internalDate");
        if (!internal.empty())
            e.timestamp = (uint64_t)(strtoull(internal.c_str(), nullptr, 10) / 1000ULL);

        cJSON *lids = cJSON_GetObjectItemCaseSensitive(j, "labelIds");
        if (cJSON_IsArray(lids))
        {
            cJSON *l = nullptr;
            cJSON_ArrayForEach(l, lids)
                if (cJSON_IsString(l) && l->valuestring && strcmp(l->valuestring, "IMPORTANT") == 0)
                    e.important = true;
        }

        cJSON *payload = cJSON_GetObjectItemCaseSensitive(j, "payload");
        cJSON *headers = payload ? cJSON_GetObjectItemCaseSensitive(payload, "headers") : nullptr;
        std::string from;
        if (cJSON_IsArray(headers))
        {
            cJSON *h = nullptr;
            cJSON_ArrayForEach(h, headers)
            {
                std::string hn = json_str(h, "name");
                if (ci_equal(hn, "From")) from = json_str(h, "value");
                else if (ci_equal(hn, "Subject")) e.subject = json_str(h, "value");
            }
        }
        parse_from(from, e.displayName, e.emailAddress);

        out.push_back(e);
        cJSON_Delete(j);
    }

    if (!newest)
        std::reverse(out.begin(), out.end());
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGMAIL::message_body(const std::string &folder, uint32_t seq,
                                               std::string &out)
{
    bool ok = false;
    std::string id = message_id_for_seq(folder, seq, ok);
    if (!ok)
    {
        _last_http = 404;
        return FUJI_ERROR::UNSPECIFIED;
    }

    cJSON *j = get_message_full(id);
    if (!j)
        return FUJI_ERROR::UNSPECIFIED;

    cJSON *payload = cJSON_GetObjectItemCaseSensitive(j, "payload");
    extract_body(payload, out);
    cJSON_Delete(j);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGMAIL::attachment_index(const std::string &folder, uint32_t seq,
                                                   std::vector<MailboxAttachmentEntry> &out)
{
    bool ok = false;
    std::string id = message_id_for_seq(folder, seq, ok);
    if (!ok)
    {
        _last_http = 404;
        return FUJI_ERROR::UNSPECIFIED;
    }

    cJSON *j = get_message_full(id);
    if (!j)
        return FUJI_ERROR::UNSPECIFIED;

    cJSON *payload = cJSON_GetObjectItemCaseSensitive(j, "payload");

    // Entry 0 is the primary body.
    MailboxAttachmentEntry body;
    body.attachmentNum = 0;
    body.displayName = "body";
    std::string b;
    if (find_text_part(payload, "text/plain", b))
        body.mimeType = "text/plain";
    else if (find_text_part(payload, "text/html", b))
        body.mimeType = "text/html";
    body.length = b.size();
    out.push_back(body);

    // Real attachments follow, numbered from 1.
    std::vector<cJSON *> atts;
    collect_attachments(payload, atts);
    uint8_t n = 1;
    for (cJSON *att : atts)
    {
        MailboxAttachmentEntry e;
        e.attachmentNum = n++;
        e.fileName = json_str(att, "filename");
        e.displayName = e.fileName;
        e.mimeType = json_str(att, "mimeType");
        cJSON *bd = cJSON_GetObjectItemCaseSensitive(att, "body");
        cJSON *sz = bd ? cJSON_GetObjectItemCaseSensitive(bd, "size") : nullptr;
        e.length = (sz && cJSON_IsNumber(sz)) ? (uint64_t)sz->valuedouble : 0;
        out.push_back(e);
    }

    cJSON_Delete(j);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGMAIL::attachment_data(const std::string &folder, uint32_t seq,
                                                  uint8_t attach, std::string &out)
{
    bool ok = false;
    std::string id = message_id_for_seq(folder, seq, ok);
    if (!ok)
    {
        _last_http = 404;
        return FUJI_ERROR::UNSPECIFIED;
    }

    cJSON *j = get_message_full(id);
    if (!j)
        return FUJI_ERROR::UNSPECIFIED;

    cJSON *payload = cJSON_GetObjectItemCaseSensitive(j, "payload");

    if (attach == 0)
    {
        extract_body(payload, out);
        cJSON_Delete(j);
        return FUJI_ERROR::NONE;
    }

    std::vector<cJSON *> atts;
    collect_attachments(payload, atts);
    if ((int)(attach - 1) >= (int)atts.size())
    {
        cJSON_Delete(j);
        _last_http = 404;
        return FUJI_ERROR::UNSPECIFIED;
    }

    cJSON *att = atts[attach - 1];
    cJSON *bd = cJSON_GetObjectItemCaseSensitive(att, "body");
    std::string inlineData = json_str(bd, "data");
    std::string attId = json_str(bd, "attachmentId");
    cJSON_Delete(j); // strings copied; safe to free the message now

    if (!inlineData.empty())
    {
        out = decode_b64url(inlineData);
        return FUJI_ERROR::NONE;
    }
    if (attId.empty())
        return FUJI_ERROR::UNSPECIFIED;

    std::string resp = api_get(std::string(GMAIL_BASE) + "/messages/" + id + "/attachments/" + attId);
    if (resp.empty())
        return FUJI_ERROR::UNSPECIFIED;

    cJSON *aj = cJSON_Parse(resp.c_str());
    std::string d = json_str(aj, "data");
    cJSON_Delete(aj);
    out = decode_b64url(d);
    return FUJI_ERROR::NONE;
}

void NetworkProtocolGMAIL::mailbox_error_to_error()
{
    switch (_last_http)
    {
    case 401:
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        break;
    case 403:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case 404:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    case 0:
        error = NDEV_STATUS::SERVICE_NOT_AVAILABLE;
        break;
    default:
        error = (_last_http >= 500) ? NDEV_STATUS::SERVICE_NOT_AVAILABLE : NDEV_STATUS::GENERAL;
        break;
    }
}
