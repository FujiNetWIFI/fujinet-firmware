/**
 * NetworkProtocolS3
 *
 * Amazon S3 / S3-compatible object storage protocol adapter for FujiNet.
 */

#include "S3.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "../encoding/hash.h"
#include "status_error_codes.h"
#include "utils.h"

// ─── construction ────────────────────────────────────────────────────────────

NetworkProtocolS3::NetworkProtocolS3(std::string *rx_buf,
                                     std::string *tx_buf,
                                     std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented  = true;
    rmdir_implemented  = true;
    Debug_printf("NetworkProtocolS3::ctor\r\n");
}

NetworkProtocolS3::~NetworkProtocolS3()
{
    Debug_printf("NetworkProtocolS3::dtor\r\n");
}

// ─── SigV4 / encoding helpers ─────────────────────────────────────────────────

std::string NetworkProtocolS3::uri_encode(const std::string &s, bool encode_slash)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else if (c == '/' && !encode_slash)
            out += '/';
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string NetworkProtocolS3::sha256_hex(const std::string &data)
{
    Hash h; // empty key => plain SHA256
    h.add_data(data);
    h.compute(Hash::Algorithm::SHA256, true);
    return h.output_hex();
}

std::vector<uint8_t> NetworkProtocolS3::hmac_sha256(const std::vector<uint8_t> &key,
                                                    const std::string &msg)
{
    Hash h;
    h.key.assign(key.begin(), key.end()); // non-empty key => HMAC-SHA256
    h.add_data(msg);
    h.compute(Hash::Algorithm::SHA256, true);
    return h.output_binary();
}

// ─── URL / key parsing ────────────────────────────────────────────────────────

bool NetworkProtocolS3::parse_url(PeoplesUrlParser *url)
{
    // Region: ?region=.. -> config -> us-east-1 (MinIO default).
    _region = url->queryParam("region", "");
    if (_region.empty())
        _region = Config.get_s3_region();
    if (_region.empty())
        _region = "us-east-1";

    // Transport: ?tls=0|1 -> config (default https).
    std::string tls = url->queryParam("tls", "");
    if (tls == "0" || tls == "false" || tls == "no")
        _use_ssl = false;
    else if (tls == "1" || tls == "true" || tls == "yes")
        _use_ssl = true;
    else
        _use_ssl = Config.get_s3_use_ssl();

    // Authority (host[:port]) used for both the URL and the signed Host header.
    if (url->host.empty())
        _authority = Config.get_s3_endpoint(); // may already include :port
    else
    {
        _authority = url->host;
        if (!url->port.empty())
            _authority += ":" + url->port;
    }
    // Drop a redundant default port so Host matches what the client sends.
    std::string defport = _use_ssl ? ":443" : ":80";
    if (_authority.size() > defport.size() &&
        _authority.compare(_authority.size() - defport.size(), defport.size(), defport) == 0)
        _authority.erase(_authority.size() - defport.size());

    // Credentials: URL userinfo overrides the [S3] config section.
    _access_key = (login != nullptr && !login->empty()) ? *login : Config.get_s3_access_key();
    _secret_key = (password != nullptr && !password->empty()) ? *password : Config.get_s3_secret_key();

    // Bucket = first path segment.
    std::string p = url->path;
    size_t s = p.find_first_not_of('/');
    if (s == std::string::npos)
    {
        _bucket.clear();
        return false;
    }
    p = p.substr(s);
    size_t slash = p.find('/');
    _bucket = (slash == std::string::npos) ? p : p.substr(0, slash);

    return !_authority.empty() && !_bucket.empty();
}

std::string NetworkProtocolS3::key_from_path(const std::string &path)
{
    // "/bucket/a/b" -> "a/b" ; "/bucket" -> "".
    std::string p = path;
    size_t s = p.find_first_not_of('/');
    if (s == std::string::npos)
        return "";
    p = p.substr(s);
    size_t slash = p.find('/');
    if (slash == std::string::npos)
        return "";
    return p.substr(slash + 1);
}

std::string NetworkProtocolS3::base_url()
{
    return (_use_ssl ? "https://" : "http://") + _authority;
}

// ─── request driver ───────────────────────────────────────────────────────────

int NetworkProtocolS3::sign_and_send(S3_HTTP_CLIENT &client,
                                     const char *method,
                                     const std::string &canonical_path,
                                     const std::map<std::string, std::string> &query,
                                     const std::vector<std::pair<std::string, std::string>> &amz_headers,
                                     const std::vector<std::pair<std::string, std::string>> &unsigned_headers,
                                     const char *body, int body_len)
{
    const std::string payload_hash = "UNSIGNED-PAYLOAD";

    // Timestamps in UTC (clock is SNTP-synced).
    time_t now = time(nullptr);
    struct tm *g = gmtime(&now);
    char amz_date[20], datestamp[12];
    strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", g);
    strftime(datestamp, sizeof(datestamp), "%Y%m%d", g);

    // Canonical URI + query string (same encoding used to build the URL).
    std::string canonical_uri = uri_encode(canonical_path, false);
    std::string canonical_query;
    for (const auto &kv : query)
    {
        if (!canonical_query.empty())
            canonical_query += "&";
        canonical_query += uri_encode(kv.first, true) + "=" + uri_encode(kv.second, true);
    }

    // Signed headers: host, x-amz-date, x-amz-content-sha256, plus any x-amz-*.
    std::map<std::string, std::string> sheaders;
    sheaders["host"] = _authority;
    sheaders["x-amz-content-sha256"] = payload_hash;
    sheaders["x-amz-date"] = amz_date;
    for (const auto &h : amz_headers)
    {
        std::string name = h.first;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        sheaders[name] = h.second;
    }
    std::string canonical_headers, signed_headers;
    for (const auto &kv : sheaders)
    {
        canonical_headers += kv.first + ":" + kv.second + "\n";
        if (!signed_headers.empty())
            signed_headers += ";";
        signed_headers += kv.first;
    }

    std::string canonical_request =
        std::string(method) + "\n" + canonical_uri + "\n" + canonical_query + "\n" +
        canonical_headers + "\n" + signed_headers + "\n" + payload_hash;

    std::string scope = std::string(datestamp) + "/" + _region + "/s3/aws4_request";
    std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" + std::string(amz_date) + "\n" + scope + "\n" +
        sha256_hex(canonical_request);

    // Derive the signing key and sign.
    std::string ks = "AWS4" + _secret_key;
    std::vector<uint8_t> key(ks.begin(), ks.end());
    key = hmac_sha256(key, datestamp);
    key = hmac_sha256(key, _region);
    key = hmac_sha256(key, "s3");
    key = hmac_sha256(key, "aws4_request");

    Hash sig;
    sig.key.assign(key.begin(), key.end());
    sig.add_data(string_to_sign);
    sig.compute(Hash::Algorithm::SHA256, true);
    std::string signature = sig.output_hex();

    std::string authorization =
        "AWS4-HMAC-SHA256 Credential=" + _access_key + "/" + scope +
        ", SignedHeaders=" + signed_headers + ", Signature=" + signature;

    // Build and issue the request.
    std::string url = base_url() + canonical_uri;
    if (!canonical_query.empty())
        url += "?" + canonical_query;

    if (!client.begin(url))
    {
        Debug_printf("S3: begin() failed for %s\r\n", url.c_str());
        return -1;
    }
    client.set_header("x-amz-date", amz_date);
    client.set_header("x-amz-content-sha256", payload_hash.c_str());
    client.set_header("Authorization", authorization.c_str());
    for (const auto &h : amz_headers)
        client.set_header(h.first.c_str(), h.second.c_str());
    for (const auto &h : unsigned_headers)
        client.set_header(h.first.c_str(), h.second.c_str());

    int status;
    if (strcmp(method, "GET") == 0)
        status = client.GET();
    else if (strcmp(method, "HEAD") == 0)
        status = client.HEAD();
    else if (strcmp(method, "PUT") == 0)
        status = client.PUT(body ? body : "", body_len);
    else if (strcmp(method, "DELETE") == 0)
        status = client.DELETE();
    else
        status = -1;

    Debug_printf("S3: %s %s -> %d\r\n", method, canonical_uri.c_str(), status);
    return status;
}

int NetworkProtocolS3::s3_do(const char *method,
                             const std::string &canonical_path,
                             const std::map<std::string, std::string> &query,
                             const std::vector<std::pair<std::string, std::string>> &amz_headers,
                             const char *body, int body_len,
                             std::string *out_body)
{
    S3_HTTP_CLIENT client;
    int status = sign_and_send(client, method, canonical_path, query, amz_headers, {}, body, body_len);
    _last_status = status;

    if (out_body != nullptr && status > 0)
    {
        uint8_t buf[512];
        int n;
        while ((n = client.read(buf, sizeof(buf))) > 0)
            out_body->append((char *)buf, n);
    }
    client.close();
    return status;
}

// ─── ListObjectsV2 XML parsing ────────────────────────────────────────────────

std::string NetworkProtocolS3::extract_tag(const std::string &xml, const std::string &tag, size_t &pos)
{
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t s = xml.find(open, pos);
    if (s == std::string::npos)
        return "";
    s += open.size();
    size_t e = xml.find(close, s);
    if (e == std::string::npos)
        return "";
    pos = e + close.size();
    return xml.substr(s, e - s);
}

static std::string xml_unescape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        if (s[i] == '&')
        {
            if (s.compare(i, 5, "&amp;") == 0)      { out += '&'; i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)       { out += '<'; i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)       { out += '>'; i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0)     { out += '"'; i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0)     { out += '\''; i += 6; continue; }
        }
        out += s[i++];
    }
    return out;
}

std::string NetworkProtocolS3::parse_list_xml(const std::string &xml, const std::string &prefix)
{
    // CommonPrefixes -> subdirectories.
    size_t pos = 0;
    while ((pos = xml.find("<CommonPrefixes>", pos)) != std::string::npos)
    {
        size_t end = xml.find("</CommonPrefixes>", pos);
        if (end == std::string::npos)
            break;
        std::string block = xml.substr(pos, end - pos);
        size_t p = 0;
        std::string full = xml_unescape(extract_tag(block, "Prefix", p));
        if (full.size() >= prefix.size() && full.compare(0, prefix.size(), prefix) == 0)
            full = full.substr(prefix.size());
        if (!full.empty() && full.back() == '/')
            full.pop_back();
        if (!full.empty())
        {
            S3Entry e;
            e.name = full;
            e.is_dir = true;
            _dir_entries.push_back(e);
        }
        pos = end + 1;
    }

    // Contents -> files.
    pos = 0;
    while ((pos = xml.find("<Contents>", pos)) != std::string::npos)
    {
        size_t end = xml.find("</Contents>", pos);
        if (end == std::string::npos)
            break;
        std::string block = xml.substr(pos, end - pos);
        size_t pk = 0, psz = 0;
        std::string k = xml_unescape(extract_tag(block, "Key", pk));
        std::string sz = extract_tag(block, "Size", psz);
        if (k.size() >= prefix.size() && k.compare(0, prefix.size(), prefix) == 0)
            k = k.substr(prefix.size());
        // Skip the folder marker itself (leaf empty) and deeper keys (still have '/').
        if (!k.empty() && k.find('/') == std::string::npos)
        {
            S3Entry e;
            e.name = k;
            e.is_dir = false;
            e.size = atol(sz.c_str());
            _dir_entries.push_back(e);
        }
        pos = end + 1;
    }

    size_t pt = 0;
    if (extract_tag(xml, "IsTruncated", pt) == "true")
    {
        size_t pc = 0;
        return extract_tag(xml, "NextContinuationToken", pc);
    }
    return "";
}

// ─── NetworkProtocolFS overrides ─────────────────────────────────────────────

fujiError_t NetworkProtocolS3::mount(PeoplesUrlParser *url)
{
    Debug_printf("NetworkProtocolS3::mount(%s)\r\n", url->url.c_str());

    if (!parse_url(url))
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return FUJI_ERROR::UNSPECIFIED;
    }
    if (_access_key.empty() || _secret_key.empty())
    {
        Debug_printf("NetworkProtocolS3::mount() - missing credentials\r\n");
        error = NDEV_STATUS::NOT_CONNECTED;
        return FUJI_ERROR::UNSPECIFIED;
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::umount()
{
    _http.close();
    _write_buf.clear();
    _write_buf.shrink_to_fit();
    _dir_entries.clear();
    _dir_idx = 0;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::stat()
{
    std::string key = key_from_path(opened_url->path);
    if (key.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    S3_HTTP_CLIENT client;
    int status = sign_and_send(client, "HEAD", "/" + _bucket + "/" + key, {}, {}, {}, nullptr, 0);
    _last_status = status;
    int cl = client.content_length();
    client.close();

    if (status >= 200 && status < 300)
    {
        fileSize = cl >= 0 ? cl : 0;
        return FUJI_ERROR::NONE;
    }

    if (status == 404)
        error = NDEV_STATUS::FILE_NOT_FOUND;
    else
        fserror_to_error();
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolS3::open_file_handle()
{
    Debug_printf("NetworkProtocolS3::open_file_handle() mode=%d\r\n", (int)streamMode);

    if (streamMode == ACCESS_MODE::WRITE || streamMode == ACCESS_MODE::APPEND)
    {
        _write_buf.clear();

        // S3 has no append; emulate by downloading the current object first.
        if (streamMode == ACCESS_MODE::APPEND)
        {
            std::string key = key_from_path(opened_url->path);
            if (!key.empty())
            {
                std::string body;
                int status = s3_do("GET", "/" + _bucket + "/" + key, {}, {}, nullptr, 0, &body);
                if (status >= 200 && status < 300)
                    _write_buf.assign(body.begin(), body.end());
            }
        }
        return FUJI_ERROR::NONE;
    }

    // READ / READWRITE: signed streaming GET; keep _http open for read_file_handle.
    std::string key = key_from_path(opened_url->path);
    if (key.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    int status = sign_and_send(_http, "GET", "/" + _bucket + "/" + key, {}, {}, {}, nullptr, 0);
    _last_status = status;
    if (status < 200 || status >= 300)
    {
        if (status == 404)
            error = NDEV_STATUS::FILE_NOT_FOUND;
        else
            fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    int cl = _http.content_length();
    if (cl >= 0)
        fileSize = cl;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::read_file_handle(uint8_t *buf, unsigned short len)
{
    // Base read_file() decrements fileSize by len after we return, so guard here
    // and do not decrement ourselves (avoids double-count / negative wrap).
    if (fileSize <= 0)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    unsigned short got = 0;
    while (got < len)
    {
        int n = _http.read(buf + got, len - got);
        if (n <= 0)
            break;
        got += n;
    }

    if (got == 0)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::write_file_handle(uint8_t *buf, unsigned short len)
{
    if (_write_buf.size() + len > WRITE_BUF_LIMIT)
    {
        Debug_printf("S3: write buffer limit (%u) reached\r\n", (unsigned)WRITE_BUF_LIMIT);
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        return FUJI_ERROR::UNSPECIFIED;
    }
    _write_buf.insert(_write_buf.end(), buf, buf + len);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::close_file_handle()
{
    if (streamMode == ACCESS_MODE::WRITE || streamMode == ACCESS_MODE::APPEND)
    {
        std::string key = key_from_path(opened_url->path);
        if (key.empty())
        {
            error = NDEV_STATUS::INVALID_DEVICESPEC;
            return FUJI_ERROR::UNSPECIFIED;
        }

        const char *body = _write_buf.empty() ? "" : (const char *)_write_buf.data();
        int status = s3_do("PUT", "/" + _bucket + "/" + key, {}, {}, body, (int)_write_buf.size(), nullptr);

        _write_buf.clear();
        _write_buf.shrink_to_fit();

        if (status < 200 || status >= 300)
        {
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
        return FUJI_ERROR::NONE;
    }

    _http.close();
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::open_dir_handle()
{
    _dir_entries.clear();
    _dir_idx = 0;

    // Listing prefix is the directory portion of the key (bucket stripped).
    std::string prefix = key_from_path(dir);

    Debug_printf("NetworkProtocolS3::open_dir_handle() bucket=%s prefix=%s\r\n",
                 _bucket.c_str(), prefix.c_str());

    std::string cont;
    do
    {
        std::map<std::string, std::string> q;
        q["list-type"] = "2";
        q["delimiter"] = "/";
        if (!prefix.empty())
            q["prefix"] = prefix;
        if (!cont.empty())
            q["continuation-token"] = cont;

        std::string body;
        int status = s3_do("GET", "/" + _bucket, q, {}, nullptr, 0, &body);
        if (status < 200 || status >= 300)
        {
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
        cont = parse_list_xml(body, prefix);
    } while (!cont.empty());

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolS3::read_dir_entry(char *buf, unsigned short len)
{
    while (_dir_idx < _dir_entries.size())
    {
        S3Entry &e = _dir_entries[_dir_idx++];

        // Wildcard filter applies to files only; directories always list.
        if (!e.is_dir && !filename.empty() && filename != "*" &&
            !util_wildcard_match(e.name.c_str(), filename.c_str()))
            continue;

        strncpy(buf, e.name.c_str(), len - 1);
        buf[len - 1] = '\0';
        fileSize = (int)e.size;
        is_directory = e.is_dir;
        is_locked = false;
        mode = e.is_dir ? 0755 : 0644;
        return FUJI_ERROR::NONE;
    }

    error = NDEV_STATUS::END_OF_FILE;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolS3::close_dir_handle()
{
    _dir_entries.clear();
    _dir_idx = 0;
    return FUJI_ERROR::NONE;
}

// ─── file operations (del / mkdir / rmdir / rename) ───────────────────────────

fujiError_t NetworkProtocolS3::del(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    std::string key = key_from_path(url->path);
    if (key.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        umount();
        return FUJI_ERROR::UNSPECIFIED;
    }

    int status = s3_do("DELETE", "/" + _bucket + "/" + key, {}, {}, nullptr, 0, nullptr);
    umount();
    return (status == 200 || status == 204) ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolS3::mkdir(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    // S3 folders are zero-byte marker objects whose key ends in '/'.
    std::string key = key_from_path(url->path);
    if (!key.empty() && key.back() != '/')
        key += "/";

    int status = s3_do("PUT", "/" + _bucket + "/" + key, {}, {}, "", 0, nullptr);
    umount();
    return (status >= 200 && status < 300) ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolS3::rmdir(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    std::string key = key_from_path(url->path);
    if (!key.empty() && key.back() != '/')
        key += "/";

    int status = s3_do("DELETE", "/" + _bucket + "/" + key, {}, {}, nullptr, 0, nullptr);
    umount();
    return (status == 200 || status == 204) ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolS3::rename(PeoplesUrlParser *url)
{
    // Base fills filename/destFilename (full URL paths) from the "old,new" spec.
    if (NetworkProtocolFS::rename(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::NONE;

    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    std::string old_key = key_from_path(filename);
    std::string new_key = key_from_path(destFilename);
    if (old_key.empty() || new_key.empty())
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        umount();
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Server-side copy (source must be URI-encoded) then delete the original.
    std::vector<std::pair<std::string, std::string>> amz = {
        {"x-amz-copy-source", uri_encode("/" + _bucket + "/" + old_key, false)}};
    int status = s3_do("PUT", "/" + _bucket + "/" + new_key, {}, amz, "", 0, nullptr);

    if (status >= 200 && status < 300)
        s3_do("DELETE", "/" + _bucket + "/" + old_key, {}, {}, nullptr, 0, nullptr);

    umount();
    return (status >= 200 && status < 300) ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

void NetworkProtocolS3::fserror_to_error()
{
    switch (_last_status)
    {
    case 200:
    case 201:
    case 204:
    case 206:
        error = NDEV_STATUS::SUCCESS;
        break;
    case 403:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case 404:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    case 409:
        error = NDEV_STATUS::FILE_EXISTS;
        break;
    case 0:
    case -1:
        error = NDEV_STATUS::NOT_CONNECTED;
        break;
    default:
        error = NDEV_STATUS::GENERAL;
        break;
    }
}
