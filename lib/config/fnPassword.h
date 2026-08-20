/* FujiNet device password

Guards sensitive operations (currently the password-protected web pages).

The password is never kept in the clear and never touches the SD card: only a
random salt and a salted, iterated SHA-256 digest are stored, in flash. On
ESP32 that means the NVS partition; on FujiNet-PC it means a small file in the
flash filesystem directory.

Each stored record is stamped with the identity of the firmware that wrote it
(the app image's ELF SHA-256 on ESP32). On startup a record written by a
different firmware image is discarded, so re-flashing the firmware clears any
previously set password.

A forgotten password can also be recovered without re-flashing: see
apply_sd_file() below.
*/
#ifndef FNPASSWORD_H
#define FNPASSWORD_H

#include <cstddef>
#include <string>

class FNPassword
{
public:
    static const size_t MIN_LENGTH = 4;
    static const size_t MAX_LENGTH = 64;

    // Name of the recovery file looked for in the root of the SD card.
    static const char *const SD_FILENAME;

    // Load the stored password record, discarding it if it belongs to a
    // different firmware image, then apply any SD recovery file. Call once at
    // startup, after fsFlash.start() and fnSDFAT.start().
    void setup();

    bool is_set() const { return !_hash.empty(); }

    // True only when a password is set and 'password' matches it.
    bool verify(const std::string &password) const;

    // Set or change the password. 'old_password' is ignored when no password
    // is currently set. On failure 'error' describes why.
    bool change(const std::string &old_password, const std::string &new_password, std::string &error);

    // Remove the password. Requires the current one.
    bool remove(const std::string &old_password, std::string &error);

    // Apply a recovery file named SD_FILENAME left in the root of the SD card,
    // if there is one. Its first line becomes the password, replacing any
    // existing one; an empty file clears the password instead. The file is
    // deleted once it has been read, so the credential does not linger on
    // removable media and the change is not re-applied on every boot.
    //
    // This is a deliberate physical-access escape hatch for a forgotten
    // password. It grants nothing that pulling the card and re-flashing the
    // firmware would not already grant.
    void apply_sd_file();

    // Verify 'password' and start a new session. Returns the new session token,
    // or an empty string when the password is wrong or no password is set.
    // Any previously issued token is invalidated.
    std::string login(const std::string &password);

    // True only when a session token exists and 'token' matches it.
    bool check_session(const std::string &token) const;

    // Invalidate the current session token.
    void logout();

    // Check an HTTP "Authorization" header carrying Basic credentials
    // (e.g. "Basic dTpw"). The username is not checked - any value, including
    // an empty one, is accepted - so only the password matters. False when no
    // password is set.
    //
    // This exists solely for WebDAV: a DAV client cannot run the cookie login
    // flow, so /dav/* accepts Basic as an alternative. Nothing else should use
    // it - the rest of the web UI is cookie-only.
    //
    // DAV clients re-authenticate on every request, so a successful check is
    // remembered briefly to avoid running the 4096-round derivation per
    // PROPFIND. Only a salted fingerprint of the header is retained, never the
    // credential itself.
    bool check_basic_auth(const char *header_value) const;

private:
    std::string _fwid;    // firmware image the stored record belongs to
    std::string _salt;    // hex
    std::string _hash;    // hex
    std::string _token;   // hex, empty when no active session

    // Cache of the last accepted Basic header, as a per-boot-salted digest.
    mutable std::string _basic_fp;
    mutable long long _basic_fp_at = 0; // seconds, 0 when empty

    void load();
    bool store();
    void wipe();
    void forget_basic_auth();

    // Store 'new_password' without checking the current one. Length is still
    // validated. Callers are responsible for authorizing the change.
    bool set_unchecked(const std::string &new_password, std::string &error);

    static std::string firmware_id();
    static std::string make_salt();
    static std::string make_token();
    static std::string derive(const std::string &salt_hex, const std::string &password);
    static std::string basic_fingerprint(const char *header_value);
};

extern FNPassword fnPassword;

#endif // FNPASSWORD_H
