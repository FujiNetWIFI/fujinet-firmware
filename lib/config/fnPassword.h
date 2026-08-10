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

    // Load the stored password record, discarding it if it belongs to a
    // different firmware image. Call once at startup, after fsFlash.start().
    void setup();

    bool is_set() const { return !_hash.empty(); }

    // True only when a password is set and 'password' matches it.
    bool verify(const std::string &password) const;

    // Set or change the password. 'old_password' is ignored when no password
    // is currently set. On failure 'error' describes why.
    bool change(const std::string &old_password, const std::string &new_password, std::string &error);

    // Remove the password. Requires the current one.
    bool remove(const std::string &old_password, std::string &error);

    // Check the value of an HTTP "Authorization" header (e.g. "Basic dTpw").
    // The username is not checked - any value, including an empty one, is
    // accepted - so only the password matters. False when no password is set.
    bool check_basic_auth(const char *header_value) const;

private:
    std::string _fwid; // firmware image the stored record belongs to
    std::string _salt; // hex
    std::string _hash; // hex

    void load();
    bool store();
    void wipe();

    static std::string firmware_id();
    static std::string make_salt();
    static std::string derive(const std::string &salt_hex, const std::string &password);
};

extern FNPassword fnPassword;

#endif // FNPASSWORD_H
