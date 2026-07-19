#ifndef NETWORKPROTOCOLS3_H
#define NETWORKPROTOCOLS3_H

#include "FS.h"

#include <map>
#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#define S3_HTTP_CLIENT fnHttpClient
#else
#include "../http/mgHttpClient.h"
#define S3_HTTP_CLIENT mgHttpClient
#endif

/**
 * NetworkProtocolS3
 *
 * Amazon S3 / S3-compatible (MinIO, Ceph RGW, Wasabi, ...) protocol adapter.
 *
 * URL format:
 *   S3://[ACCESSKEY:SECRETKEY@]endpoint[:port]/bucket/object/key[?region=..&tls=0|1]
 *
 * Path-style addressing is used ("https://endpoint/bucket/key") so the adapter
 * works uniformly against AWS and self-hosted endpoints on custom host/port.
 *
 * Credentials come from the URL userinfo when present, otherwise fall back to
 * the [S3] fnConfig section. Requests are signed with AWS Signature V4.
 *
 * Writes are buffered and uploaded with a single PUT on close.
 */
class NetworkProtocolS3 : public NetworkProtocolFS
{
public:
    NetworkProtocolS3(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolS3();

    NetworkProtocolS3(const NetworkProtocolS3 &) = delete;
    NetworkProtocolS3 &operator=(const NetworkProtocolS3 &) = delete;

    fujiError_t rename(PeoplesUrlParser *url) override;
    fujiError_t del(PeoplesUrlParser *url) override;
    fujiError_t mkdir(PeoplesUrlParser *url) override;
    fujiError_t rmdir(PeoplesUrlParser *url) override;

protected:
    fujiError_t open_file_handle() override;
    fujiError_t open_dir_handle() override;
    fujiError_t mount(PeoplesUrlParser *url) override;
    fujiError_t umount() override;
    void fserror_to_error() override;
    fujiError_t read_file_handle(uint8_t *buf, unsigned short len) override;
    fujiError_t read_dir_entry(char *buf, unsigned short len) override;
    fujiError_t write_file_handle(uint8_t *buf, unsigned short len) override;
    fujiError_t close_file_handle() override;
    fujiError_t close_dir_handle() override;
    fujiError_t stat() override;

private:
    // A single parsed ListObjectsV2 result entry.
    struct S3Entry
    {
        std::string name;   // leaf name (prefix stripped)
        long size = 0;
        bool is_dir = false;
    };

    // Connection parameters resolved by parse_url()/mount().
    std::string _authority;   // host[:port] as used for URL and Host header
    std::string _region;
    std::string _access_key;
    std::string _secret_key;
    std::string _bucket;
    bool _use_ssl = true;

    // Persistent client used to stream a file download across read() calls.
    S3_HTTP_CLIENT _http;

    // Data accumulates here on write; uploaded by close_file_handle().
    std::vector<uint8_t> _write_buf;

    // Parsed directory listing state.
    std::vector<S3Entry> _dir_entries;
    size_t _dir_idx = 0;

    // Last HTTP status seen (drives fserror_to_error()).
    int _last_status = 0;

    // Maximum bytes buffered for a single write (single-PUT upload).
    static constexpr size_t WRITE_BUF_LIMIT = 512 * 1024;

    // Parse endpoint/bucket/region/tls and resolve credentials from url + config.
    bool parse_url(PeoplesUrlParser *url);

    // The S3 object key for a given URL path ("/bucket/a/b" -> "a/b").
    std::string key_from_path(const std::string &path);

    // "https://authority" (or http).
    std::string base_url();

    // Sign an S3 request with SigV4 and issue it on `client`.
    // canonical_path: unencoded path ("/bucket/key"); query: unencoded params.
    // amz_headers: extra x-amz-* headers (signed). unsigned_headers: sent, not
    // signed (e.g. Range). Returns HTTP status (<0 on transport failure).
    // Does not read the response body; the caller drains `client`.
    int sign_and_send(S3_HTTP_CLIENT &client,
                      const char *method,
                      const std::string &canonical_path,
                      const std::map<std::string, std::string> &query,
                      const std::vector<std::pair<std::string, std::string>> &amz_headers,
                      const std::vector<std::pair<std::string, std::string>> &unsigned_headers,
                      const char *body, int body_len);

    // Convenience: run a one-shot request on a local client and collect the body.
    int s3_do(const char *method,
              const std::string &canonical_path,
              const std::map<std::string, std::string> &query,
              const std::vector<std::pair<std::string, std::string>> &amz_headers,
              const char *body, int body_len,
              std::string *out_body);

    // Parse one page of ListObjectsV2 XML into _dir_entries; returns the
    // NextContinuationToken (empty when the listing is complete).
    std::string parse_list_xml(const std::string &xml, const std::string &prefix);

    // SigV4 / encoding helpers.
    static std::string uri_encode(const std::string &s, bool encode_slash);
    static std::string sha256_hex(const std::string &data);
    static std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t> &key, const std::string &msg);
    static std::string extract_tag(const std::string &xml, const std::string &tag, size_t &pos);
};

#endif /* NETWORKPROTOCOLS3_H */
