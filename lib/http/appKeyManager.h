/* App key manager for the FujiNet web server

App keys are the small blobs of state that programs store on the FujiNet via
the OPEN/READ/WRITE APPKEY commands. They live on the SD card as
/FujiNet/<creator:4hex><app:2hex><key:2hex>.key - see _generate_appkey_filename()
in lib/device/fujiDevice/fujiDevice.cpp.

This class provides the listing, editing and deletion used by the
password-protected /appkeys page. The HTTP plumbing differs between the ESP32
and FujiNet-PC web servers, so only that part lives in httpService.cpp /
mgHttpService.cpp; everything else is shared from here.
*/
#ifndef APPKEYMANAGER_H
#define APPKEYMANAGER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#define APPKEY_DIR "/FujiNet"

// Upper bound on a posted /appkeys form. One table row costs roughly 220 bytes
// once url-encoded, so this covers a comfortably large key collection while
// still bounding what a request can make the device allocate.
#define APPKEYS_MAX_POST_SIZE 16384

struct AppKeyEntry
{
    uint16_t creator = 0;
    uint8_t app = 0;
    uint8_t key = 0;
    std::string id;               // 8 hex digits, i.e. the filename without ".key"
    std::vector<uint8_t> value;
};

class AppKeyManager
{
public:
    // All app keys currently on the SD card, sorted by id. Empty when there is
    // no SD card or no /FujiNet directory.
    static std::vector<AppKeyEntry> list();

    // How many app keys are stored. Cheaper than list() - the values are not read.
    static size_t count();

    // Apply one posted /appkeys form. Recognised fields:
    //   action        "save" | "add" | "delete:<id>" | "delete_all"
    //   id<N>/val<N>  id and new value of table row N (action=save)
    //   newcreator/newapp/newkey/newval  the key to create (action=add)
    // Rows are applied in ascending N, so if two rows carry the same id the
    // last one wins and overwrites the earlier one.
    // Returns a human-readable result message for the page to display.
    static std::string handle_post(const std::map<std::string, std::string> &postvals);

    // Complete HTML page listing the keys. 'message' is shown above the table
    // when not empty.
    static std::string render_page(const std::string &message);

private:
    // The app keys present on the SD card, without their values read.
    static std::vector<AppKeyEntry> scan();

    static std::string handle_add(const std::map<std::string, std::string> &postvals);

    static bool read_value(const std::string &id, std::vector<uint8_t> &value);
    static bool write_value(const std::string &id, const std::vector<uint8_t> &value, std::string &error);
    static bool delete_key(const std::string &id);

    // Values are usually text but may be arbitrary bytes. Printable values are
    // shown and accepted as-is; anything else round-trips as "hex:<digits>".
    static std::string value_to_field(const std::vector<uint8_t> &value);
    static bool field_to_value(const std::string &field, std::vector<uint8_t> &value, std::string &error);

    static std::string path_for(const std::string &id);
    static bool parse_id(const std::string &id, AppKeyEntry &entry);
};

#endif // APPKEYMANAGER_H
