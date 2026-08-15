#include "fnPassword.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include "../../include/debug.h"
#include "../../include/version.h"
#include "base64.h"

#ifdef ESP_PLATFORM
#include <esp_app_desc.h>
#include <esp_random.h>
#include <nvs.h>
#else
#include <random>
#include "fsFlash.h"
#endif

#if MBEDTLS_VERSION_NUMBER >= 0x03000000 || MBEDTLS_VERSION_NUMBER < 0x02070000
#define COMPAT_MBEDTLS_SHA256 mbedtls_sha256
#else
#define COMPAT_MBEDTLS_SHA256 mbedtls_sha256_ret
#endif

// Number of SHA-256 rounds used to derive the stored digest. High enough to
// make an offline attack on a dumped flash image tedious, low enough that a
// single HTTP request can afford one derivation.
#define PASSWORD_HASH_ROUNDS 4096

#define PASSWORD_SALT_BYTES 16
#define PASSWORD_TOKEN_BYTES 32

// How long a successful WebDAV Basic check is remembered, in seconds.
#define PASSWORD_BASIC_CACHE_SECONDS 60

#ifdef ESP_PLATFORM
#define PASSWORD_NVS_NAMESPACE "fnpassword"
#define PASSWORD_NVS_KEY_FWID "fwid"
#define PASSWORD_NVS_KEY_SALT "salt"
#define PASSWORD_NVS_KEY_HASH "hash"
#define PASSWORD_NVS_KEY_TOKEN "token"
#else
#define PASSWORD_FILENAME "/fnpassword"
#endif

FNPassword fnPassword;

static std::string bytes_to_hex(const uint8_t *bytes, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++)
    {
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 0x0F];
    }
    return out;
}

static size_t hex_to_bytes(const std::string &hex, uint8_t *out, size_t outsize)
{
    size_t n = hex.size() / 2;
    if (n > outsize)
        n = outsize;
    for (size_t i = 0; i < n; i++)
    {
        unsigned int v = 0;
        if (sscanf(hex.c_str() + i * 2, "%2x", &v) != 1)
            return i;
        out[i] = (uint8_t)v;
    }
    return n;
}

// Compare without leaking how many leading characters matched.
static bool constant_time_equals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

std::string FNPassword::firmware_id()
{
#ifdef ESP_PLATFORM
    // SHA-256 of the running app's ELF - changes with every firmware image.
    char sha[65] = {0};
    if (esp_app_get_elf_sha256(sha, sizeof(sha)) > 0)
        return std::string(sha);
    return std::string(FN_VERSION_FULL) + "/" + FN_VERSION_BUILD;
#else
    // FujiNet-PC has no app partition to fingerprint; the build identity of
    // this translation unit is the closest equivalent.
    return std::string(FN_VERSION_FULL) + "/" + FN_VERSION_BUILD + "/" __DATE__ " " __TIME__;
#endif
}

std::string FNPassword::make_salt()
{
    uint8_t salt[PASSWORD_SALT_BYTES];
#ifdef ESP_PLATFORM
    esp_fill_random(salt, sizeof(salt));
#else
    std::random_device rd;
    for (size_t i = 0; i < sizeof(salt); i++)
        salt[i] = (uint8_t)(rd() & 0xFF);
#endif
    return bytes_to_hex(salt, sizeof(salt));
}

std::string FNPassword::make_token()
{
    uint8_t token[PASSWORD_TOKEN_BYTES];
#ifdef ESP_PLATFORM
    esp_fill_random(token, sizeof(token));
#else
    std::random_device rd;
    for (size_t i = 0; i < sizeof(token); i++)
        token[i] = (uint8_t)(rd() & 0xFF);
#endif
    return bytes_to_hex(token, sizeof(token));
}

std::string FNPassword::derive(const std::string &salt_hex, const std::string &password)
{
    uint8_t salt[PASSWORD_SALT_BYTES] = {0};
    size_t saltlen = hex_to_bytes(salt_hex, salt, sizeof(salt));

    // Round 0 hashes salt || password, every later round re-hashes salt || digest.
    uint8_t digest[32];
    std::string seed(reinterpret_cast<const char *>(salt), saltlen);
    seed += password;
    COMPAT_MBEDTLS_SHA256(reinterpret_cast<const uint8_t *>(seed.data()), seed.size(), digest, 0);

    uint8_t block[PASSWORD_SALT_BYTES + sizeof(digest)];
    memcpy(block, salt, saltlen);
    for (int i = 1; i < PASSWORD_HASH_ROUNDS; i++)
    {
        memcpy(block + saltlen, digest, sizeof(digest));
        COMPAT_MBEDTLS_SHA256(block, saltlen + sizeof(digest), digest, 0);
    }

    return bytes_to_hex(digest, sizeof(digest));
}

void FNPassword::setup()
{
    load();

    if (_hash.empty())
        return;

    std::string current = firmware_id();
    if (_fwid != current)
    {
        Debug_println("Device password was set by different firmware - clearing");
        wipe();
        return;
    }

    Debug_println("Device password is set");
}

bool FNPassword::verify(const std::string &password) const
{
    if (_hash.empty())
        return false;
    return constant_time_equals(_hash, derive(_salt, password));
}

bool FNPassword::change(const std::string &old_password, const std::string &new_password, std::string &error)
{
    if (is_set() && !verify(old_password))
    {
        error = "Current password is incorrect";
        return false;
    }
    if (new_password.size() < MIN_LENGTH)
    {
        error = "New password must be at least " + std::to_string(MIN_LENGTH) + " characters";
        return false;
    }
    if (new_password.size() > MAX_LENGTH)
    {
        error = "New password must be at most " + std::to_string(MAX_LENGTH) + " characters";
        return false;
    }

    std::string salt = make_salt();
    std::string hash = derive(salt, new_password);

    std::string prev_fwid = _fwid, prev_salt = _salt, prev_hash = _hash, prev_token = _token;
    _fwid = firmware_id();
    _salt = salt;
    _hash = hash;
    _token.clear();

    if (!store())
    {
        _fwid = prev_fwid;
        _salt = prev_salt;
        _hash = prev_hash;
        _token = prev_token;
        error = "Could not save the new password to flash";
        return false;
    }

    forget_basic_auth();
    Debug_println("Device password changed");
    return true;
}

bool FNPassword::remove(const std::string &old_password, std::string &error)
{
    if (!is_set())
    {
        error = "No password is set";
        return false;
    }
    if (!verify(old_password))
    {
        error = "Current password is incorrect";
        return false;
    }

    wipe();
    _token.clear();
    Debug_println("Device password removed");
    return true;
}

std::string FNPassword::login(const std::string &password)
{
    if (!verify(password))
        return "";

    _token = make_token();
    if (!store())
    {
        _token.clear();
        return "";
    }
    return _token;
}

bool FNPassword::check_session(const std::string &token) const
{
    if (_token.empty())
        return false;
    return constant_time_equals(_token, token);
}

void FNPassword::logout()
{
    if (_token.empty())
        return;
    _token.clear();
    store();
}

/* Digest of a Basic header, salted with a value regenerated every boot so the
   cache is useless to anything that reads it out of RAM later. One SHA-256
   round, versus the 4096 that verifying the password properly costs. */
std::string FNPassword::basic_fingerprint(const char *header_value)
{
    static std::string boot_salt = make_token();

    std::string seed = boot_salt;
    seed += header_value;

    uint8_t digest[32];
    COMPAT_MBEDTLS_SHA256((const unsigned char *)seed.data(), seed.size(), digest, 0);
    return bytes_to_hex(digest, sizeof(digest));
}

void FNPassword::forget_basic_auth()
{
    _basic_fp.clear();
    _basic_fp_at = 0;
}

bool FNPassword::check_basic_auth(const char *header_value) const
{
    if (!is_set() || header_value == nullptr)
        return false;

    // A WebDAV client re-sends these credentials on every request, so check the
    // recent-success cache before paying for the full derivation.
    long long now = (long long)time(nullptr);
    std::string fp = basic_fingerprint(header_value);
    if (!_basic_fp.empty())
    {
        long long age = now - _basic_fp_at;
        if (age >= 0 && age < PASSWORD_BASIC_CACHE_SECONDS && constant_time_equals(_basic_fp, fp))
            return true;
    }

    // Expect: Basic <base64 of "username:password">
    while (*header_value == ' ')
        header_value++;
    static const char scheme[] = "basic";
    for (size_t i = 0; i < sizeof(scheme) - 1; i++)
    {
        if (tolower((unsigned char)header_value[i]) != scheme[i])
            return false;
    }
    const char *b64 = header_value + sizeof(scheme) - 1;
    while (*b64 == ' ')
        b64++;

    // Trim any trailing whitespace the header may carry.
    size_t b64len = strlen(b64);
    while (b64len > 0 && isspace((unsigned char)b64[b64len - 1]))
        b64len--;
    if (b64len == 0)
        return false;

    size_t decoded_len = 0;
    auto decoded = Base64::decode(b64, b64len, &decoded_len);
    if (decoded == nullptr || decoded_len == 0)
        return false;

    std::string creds(reinterpret_cast<const char *>(decoded.get()), decoded_len);
    size_t colon = creds.find(':');
    if (colon == std::string::npos)
        return false;

    // Username is deliberately not checked - only the password matters.
    if (!verify(creds.substr(colon + 1)))
        return false;

    _basic_fp = fp;
    _basic_fp_at = now;
    return true;
}

#ifdef ESP_PLATFORM

static bool nvs_read_str(nvs_handle_t handle, const char *key, std::string &out)
{
    size_t len = 0;
    if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK || len == 0)
        return false;
    std::string buf(len, '\0');
    if (nvs_get_str(handle, key, &buf[0], &len) != ESP_OK)
        return false;
    buf.resize(len > 0 ? len - 1 : 0); // drop the NUL nvs counts in len
    out = buf;
    return true;
}

void FNPassword::load()
{
    _fwid.clear();
    _salt.clear();
    _hash.clear();
    _token.clear();

    nvs_handle_t handle;
    if (nvs_open(PASSWORD_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        return; // namespace does not exist yet - no password set

    if (!nvs_read_str(handle, PASSWORD_NVS_KEY_HASH, _hash) ||
        !nvs_read_str(handle, PASSWORD_NVS_KEY_SALT, _salt) ||
        !nvs_read_str(handle, PASSWORD_NVS_KEY_FWID, _fwid))
    {
        _fwid.clear();
        _salt.clear();
        _hash.clear();
    }

    // Token is read separately - a missing token key means no active session,
    // not a corrupt record. This allows existing installs to upgrade.
    nvs_read_str(handle, PASSWORD_NVS_KEY_TOKEN, _token);

    nvs_close(handle);
}

bool FNPassword::store()
{
    nvs_handle_t handle;
    esp_err_t e = nvs_open(PASSWORD_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (e != ESP_OK)
    {
        Debug_printf("fnPassword: nvs_open failed, err = %d\r\n", e);
        return false;
    }

    bool ok = nvs_set_str(handle, PASSWORD_NVS_KEY_FWID, _fwid.c_str()) == ESP_OK &&
              nvs_set_str(handle, PASSWORD_NVS_KEY_SALT, _salt.c_str()) == ESP_OK &&
              nvs_set_str(handle, PASSWORD_NVS_KEY_HASH, _hash.c_str()) == ESP_OK &&
              nvs_set_str(handle, PASSWORD_NVS_KEY_TOKEN, _token.c_str()) == ESP_OK &&
              nvs_commit(handle) == ESP_OK;
    if (!ok)
        Debug_println("fnPassword: failed to write password to NVS");

    nvs_close(handle);
    return ok;
}

void FNPassword::wipe()
{
    _fwid.clear();
    _salt.clear();
    _hash.clear();
    _token.clear();
    forget_basic_auth();

    nvs_handle_t handle;
    if (nvs_open(PASSWORD_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return;
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
}

#else // !ESP_PLATFORM

void FNPassword::load()
{
    _fwid.clear();
    _salt.clear();
    _hash.clear();
    _token.clear();

    if (!fsFlash.exists(PASSWORD_FILENAME))
        return;

    FILE *fin = fsFlash.file_open(PASSWORD_FILENAME, "r");
    if (fin == nullptr)
    {
        Debug_println("fnPassword: could not open password file");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fin) != nullptr)
    {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        size_t eq = s.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = s.substr(0, eq);
        std::string value = s.substr(eq + 1);
        if (key == "fwid")
            _fwid = value;
        else if (key == "salt")
            _salt = value;
        else if (key == "hash")
            _hash = value;
        else if (key == "token")
            _token = value;
    }
    fclose(fin);

    if (_salt.empty() || _hash.empty())
    {
        _fwid.clear();
        _salt.clear();
        _hash.clear();
        _token.clear();
    }
}

bool FNPassword::store()
{
    FILE *fout = fsFlash.file_open(PASSWORD_FILENAME, "w");
    if (fout == nullptr)
    {
        Debug_println("fnPassword: could not write password file");
        return false;
    }
    fprintf(fout, "fwid=%s\nsalt=%s\nhash=%s\ntoken=%s\n", _fwid.c_str(), _salt.c_str(), _hash.c_str(), _token.c_str());
    fclose(fout);
    return true;
}

void FNPassword::wipe()
{
    _fwid.clear();
    _salt.clear();
    _hash.clear();
    _token.clear();
    forget_basic_auth();

    if (fsFlash.exists(PASSWORD_FILENAME))
        fsFlash.remove(PASSWORD_FILENAME);
}

#endif // ESP_PLATFORM
