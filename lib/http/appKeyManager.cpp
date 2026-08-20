#include "appKeyManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../../include/debug.h"
#include "fnFsSD.h"
#include "fnPassword.h"
#include "fujiDevice.h" // MAX_APPKEY_LEN

#define APPKEY_ID_LEN 8 // 4 hex creator + 2 hex app + 2 hex key
#define APPKEY_EXT ".key"

static bool is_hex_digits(const std::string &s, size_t len)
{
    if (s.size() != len)
        return false;
    for (char c : s)
    {
        if (!isxdigit((unsigned char)c))
            return false;
    }
    return true;
}

static std::string to_lower(const std::string &in)
{
    std::string out = in;
    for (char &c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}

static std::string html_escape(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string AppKeyManager::path_for(const std::string &id)
{
    return std::string(APPKEY_DIR) + "/" + id + APPKEY_EXT;
}

bool AppKeyManager::parse_id(const std::string &id, AppKeyEntry &entry)
{
    if (!is_hex_digits(id, APPKEY_ID_LEN))
        return false;

    unsigned int creator = 0, app = 0, key = 0;
    if (sscanf(id.c_str(), "%4x%2x%2x", &creator, &app, &key) != 3)
        return false;

    entry.creator = (uint16_t)creator;
    entry.app = (uint8_t)app;
    entry.key = (uint8_t)key;
    // The firmware always writes these filenames in lower case, so normalise to
    // match - see _generate_appkey_filename().
    entry.id = to_lower(id);
    return true;
}

bool AppKeyManager::read_value(const std::string &id, std::vector<uint8_t> &value)
{
    FILE *fin = fnSDFAT.file_open(path_for(id).c_str(), FILE_READ);
    if (fin == nullptr)
        return false;

    value.resize(MAX_APPKEY_LEN);
    size_t count = fread(value.data(), 1, MAX_APPKEY_LEN, fin);
    value.resize(count);
    fclose(fin);
    return true;
}

bool AppKeyManager::write_value(const std::string &id, const std::vector<uint8_t> &value, std::string &error)
{
    fnSDFAT.create_path(APPKEY_DIR);

    FILE *fout = fnSDFAT.file_open(path_for(id).c_str(), FILE_WRITE);
    if (fout == nullptr)
    {
        error = "could not open " + path_for(id) + " for writing";
        return false;
    }

    size_t written = value.empty() ? 0 : fwrite(value.data(), 1, value.size(), fout);
    fclose(fout);

    if (written != value.size())
    {
        error = "only wrote " + std::to_string(written) + " of " + std::to_string(value.size()) + " bytes";
        return false;
    }
    return true;
}

bool AppKeyManager::delete_key(const std::string &id)
{
    return fnSDFAT.remove(path_for(id).c_str());
}

std::string AppKeyManager::value_to_field(const std::vector<uint8_t> &value)
{
    bool printable = true;
    for (uint8_t b : value)
    {
        if (b < 0x20 || b > 0x7E)
        {
            printable = false;
            break;
        }
    }

    if (printable)
        return std::string(value.begin(), value.end());

    static const char digits[] = "0123456789abcdef";
    std::string out = "hex:";
    out.reserve(4 + value.size() * 2);
    for (uint8_t b : value)
    {
        out += digits[b >> 4];
        out += digits[b & 0x0F];
    }
    return out;
}

bool AppKeyManager::field_to_value(const std::string &field, std::vector<uint8_t> &value, std::string &error)
{
    value.clear();

    if (field.compare(0, 4, "hex:") == 0)
    {
        std::string hex = field.substr(4);
        if (hex.size() % 2 != 0 || !is_hex_digits(hex, hex.size()))
        {
            error = "hex: value must be an even number of hex digits";
            return false;
        }
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            unsigned int b = 0;
            sscanf(hex.c_str() + i, "%2x", &b);
            value.push_back((uint8_t)b);
        }
    }
    else
    {
        value.assign(field.begin(), field.end());
    }

    if (value.size() > MAX_APPKEY_LEN)
    {
        error = "value is " + std::to_string(value.size()) + " bytes, the maximum is " +
                std::to_string(MAX_APPKEY_LEN);
        return false;
    }
    return true;
}

std::vector<AppKeyEntry> AppKeyManager::scan()
{
    std::vector<AppKeyEntry> entries;

    if (!fnSDFAT.running())
        return entries;

    if (!fnSDFAT.dir_open(APPKEY_DIR, nullptr, 0))
    {
        Debug_printf("AppKeyManager: could not open %s\n", APPKEY_DIR);
        return entries;
    }

    fsdir_entry *dp;
    while ((dp = fnSDFAT.dir_read()) != nullptr)
    {
        if (dp->isDir)
            continue;

        std::string name = to_lower(dp->filename);
        if (name.size() != APPKEY_ID_LEN + strlen(APPKEY_EXT))
            continue;
        if (name.compare(APPKEY_ID_LEN, std::string::npos, APPKEY_EXT) != 0)
            continue;

        AppKeyEntry entry;
        if (!parse_id(name.substr(0, APPKEY_ID_LEN), entry))
            continue;

        entries.push_back(entry);
    }
    fnSDFAT.dir_close();

    std::sort(entries.begin(), entries.end(),
              [](const AppKeyEntry &a, const AppKeyEntry &b) { return a.id < b.id; });

    return entries;
}

std::vector<AppKeyEntry> AppKeyManager::list()
{
    std::vector<AppKeyEntry> entries = scan();

    // Read the values only after the directory is closed - the SD filesystem
    // keeps a single directory handle.
    for (AppKeyEntry &entry : entries)
        read_value(entry.id, entry.value);

    return entries;
}

size_t AppKeyManager::count()
{
    return scan().size();
}

// Turn a hand-typed hex field into a zero-padded lower case one, e.g. "f" -> "000f".
static bool pad_hex_field(const std::string &in, size_t width, std::string &out)
{
    if (in.empty() || in.size() > width || !is_hex_digits(in, in.size()))
        return false;
    out = std::string(width - in.size(), '0') + to_lower(in);
    return true;
}

std::string AppKeyManager::handle_add(const std::map<std::string, std::string> &postvals)
{
    auto field = [&postvals](const char *name) -> std::string {
        auto it = postvals.find(name);
        return (it != postvals.end()) ? it->second : std::string();
    };

    std::string creator, app, key;
    if (!pad_hex_field(field("newcreator"), 4, creator))
        return "Creator must be 1 to 4 hex digits.";
    if (!pad_hex_field(field("newapp"), 2, app))
        return "App must be 1 or 2 hex digits.";
    if (!pad_hex_field(field("newkey"), 2, key))
        return "Id must be 1 or 2 hex digits.";

    if (creator == "0000")
        return "Creator 0000 is reserved - the FujiNet rejects it when opening an app key.";

    std::string id = creator + app + key;
    AppKeyEntry entry;
    if (!parse_id(id, entry))
        return "Not a valid app key id.";

    std::vector<uint8_t> value;
    std::string error;
    if (!field_to_value(field("newval"), value, error))
        return html_escape(error) + ".";

    bool existed = fnSDFAT.exists(path_for(id).c_str());
    if (!write_value(id, value, error))
        return "Could not add app key " + id + ": " + html_escape(error) + ".";

    return (existed ? "Overwrote existing app key " : "Added app key ") + id + ".";
}

std::string AppKeyManager::handle_post(const std::map<std::string, std::string> &postvals)
{
    if (!fnSDFAT.running())
        return "No SD card is mounted - app keys are stored on the SD card.";

    auto action_it = postvals.find("action");
    std::string action = (action_it != postvals.end()) ? action_it->second : "";

    if (action == "add")
        return handle_add(postvals);

    if (action == "delete_all")
    {
        int deleted = 0, failed = 0;
        for (const AppKeyEntry &entry : list())
        {
            if (delete_key(entry.id))
                deleted++;
            else
                failed++;
        }
        if (failed)
            return "Deleted " + std::to_string(deleted) + " app key(s), " +
                   std::to_string(failed) + " could not be deleted.";
        return "Deleted " + std::to_string(deleted) + " app key(s).";
    }

    if (action.compare(0, 7, "delete:") == 0)
    {
        std::string id = action.substr(7);
        AppKeyEntry entry;
        if (!parse_id(id, entry))
            return "Not a valid app key id.";
        if (!delete_key(id))
            return "Could not delete app key " + html_escape(id) + ".";
        return "Deleted app key " + html_escape(id) + ".";
    }

    if (action != "save")
        return "Unknown action.";

    // Apply rows in ascending index so that if two rows carry the same id the
    // last one wins and overwrites the earlier one.
    int saved = 0;
    std::string errors;
    for (int i = 0;; i++)
    {
        auto id_it = postvals.find("id" + std::to_string(i));
        if (id_it == postvals.end())
            break;

        auto val_it = postvals.find("val" + std::to_string(i));
        if (val_it == postvals.end())
            continue;

        AppKeyEntry entry;
        if (!parse_id(id_it->second, entry))
        {
            errors += " " + html_escape(id_it->second) + ": not a valid app key id.";
            continue;
        }

        std::vector<uint8_t> value;
        std::string error;
        if (!field_to_value(val_it->second, value, error))
        {
            errors += " " + html_escape(entry.id) + ": " + html_escape(error) + ".";
            continue;
        }

        // Skip rows that haven't changed so untouched keys keep their timestamps
        std::vector<uint8_t> existing;
        if (read_value(entry.id, existing) && existing == value)
            continue;

        if (!write_value(entry.id, value, error))
        {
            errors += " " + html_escape(entry.id) + ": " + html_escape(error) + ".";
            continue;
        }
        saved++;
    }

    std::string message = "Saved " + std::to_string(saved) + " app key(s).";
    if (!errors.empty())
        message += errors;
    return message;
}

std::string AppKeyManager::render_page(const std::string &message)
{
    // Reuses the web UI's stylesheet and logo so the page looks like the rest
    // of the interface; only the table needs styling of its own.
    std::string page =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>FujiNet - App Keys</title>"
        "<link rel=\"stylesheet\" href=\"/file?css/core.css\">"
        "<style>"
        ".akmsg{margin:0.5rem;padding:0.5rem;background:#eee;border:1px solid #999;}"
        ".aktable{border-collapse:collapse;width:100%;}"
        ".aktable th,.aktable td{border-bottom:1px solid #ccc;padding:0.25rem 0.5rem;text-align:left;}"
        ".aktable th{background:#ccc;}"
        ".aktable td.mono{font-family:Inconsolata,monospace;}"
        ".aktable input{border:1px solid rgba(0,0,0,0.2);border-radius:0.25rem;height:2rem;"
        "padding-left:0.25rem;width:100%;font-family:Inconsolata,monospace;}"
        ".aktable .idcol{width:5rem;}"
        ".aktable .btncol{width:7rem;text-align:right;}"
        ".akbutton{border:1px solid rgba(0,0,0,0.2);height:2rem;line-height:1.9rem;"
        "border-radius:0.5rem;background:#999;color:#fff;padding:0 0.75rem;}"
        ".akactions{padding:0.75rem 0.5rem;text-align:right;}"
        ".aknote{padding:0 0.5rem 0.5rem;font-size:small;}"
        "</style></head><body><div class=\"wrapper\">"
        "<div class=\"headerlogo\"><a href=\"/\"><img src=\"/file?logo.svg\" alt=\"FujiNet\""
        " style=\"width:100%\"></a></div>"
        "<div class=\"module-container\"><div class=\"module\">"
        "<header class=\"module-header\">APP<span class=\"logowob\"></span>KEYS</header>";

    if (fnPassword.is_set())
        page += "<form method=\"post\" action=\"/logout\" style=\"margin:0.5rem;\">"
                "<button type=\"submit\" class=\"akbutton\">Log Out</button>"
                "</form>";

    if (!message.empty())
        page += "<div class=\"akmsg\">" + message + "</div>";

    if (!fnSDFAT.running())
    {
        page += "<div class=\"aknote\">No SD card is mounted. App keys are stored on the SD card "
                "under " APPKEY_DIR "/.</div>"
                "</div></div></div></body></html>";
        return page;
    }

    std::vector<AppKeyEntry> entries = list();

    page += "<form method=\"post\" action=\"/appkeys\">"
            "<table class=\"aktable\"><tr>"
            "<th class=\"idcol\">Creator</th><th class=\"idcol\">App</th><th class=\"idcol\">Id</th>"
            "<th>Value</th><th class=\"btncol\"></th></tr>";

    const std::string maxlen = std::to_string(MAX_APPKEY_LEN * 2 + 4);
    char idbuf[16];
    int row = 0;
    for (const AppKeyEntry &entry : entries)
    {
        std::string field = value_to_field(entry.value);

        snprintf(idbuf, sizeof(idbuf), "%04x", entry.creator);
        page += "<tr><td class=\"mono\">" + std::string(idbuf) + "</td>";
        snprintf(idbuf, sizeof(idbuf), "%02x", entry.app);
        page += "<td class=\"mono\">" + std::string(idbuf) + "</td>";
        snprintf(idbuf, sizeof(idbuf), "%02x", entry.key);
        page += "<td class=\"mono\">" + std::string(idbuf) + "</td>";

        std::string n = std::to_string(row++);
        page += "<td><input type=\"hidden\" name=\"id" + n + "\" value=\"" + entry.id + "\">"
                "<input type=\"text\" name=\"val" + n + "\" value=\"" + html_escape(field) +
                "\" maxlength=\"" + maxlen + "\"></td>";

        page += "<td class=\"btncol\"><button class=\"akbutton\" type=\"submit\" name=\"action\""
                " value=\"delete:" + entry.id + "\""
                " onclick=\"return confirm('Delete app key " + entry.id + "?')\">Delete</button>"
                "</td></tr>";
    }

    if (entries.empty())
        page += "<tr><td class=\"mono\" colspan=\"5\">No app keys are stored on this FujiNet.</td></tr>";

    // The row for creating a key is always present, so there is always a way to
    // add one even when the list is empty.
    page += "<tr>"
            "<td><input type=\"text\" name=\"newcreator\" maxlength=\"4\" placeholder=\"0000\"></td>"
            "<td><input type=\"text\" name=\"newapp\" maxlength=\"2\" placeholder=\"00\"></td>"
            "<td><input type=\"text\" name=\"newkey\" maxlength=\"2\" placeholder=\"00\"></td>"
            "<td><input type=\"text\" name=\"newval\" maxlength=\"" + maxlen + "\" placeholder=\"value\"></td>"
            "<td class=\"btncol\"><button class=\"akbutton\" type=\"submit\" name=\"action\""
            " value=\"add\">Add Key</button></td></tr>";

    page += "</table><div class=\"akactions\">";
    if (!entries.empty())
    {
        page += "<button class=\"akbutton\" type=\"submit\" name=\"action\" value=\"save\">"
                "Save Changes</button> "
                "<button class=\"akbutton\" type=\"submit\" name=\"action\" value=\"delete_all\""
                " onclick=\"return confirm('Delete ALL app keys? This cannot be undone.')\">"
                "Delete All</button>";
    }
    page += "</div></form>";

    page += "<div class=\"aknote\">Values are shown as text. A value containing bytes that are not "
            "printable ASCII is shown as <code>hex:&lt;digits&gt;</code>, and may be edited and "
            "saved in that form. The maximum length is " + std::to_string(MAX_APPKEY_LEN) +
            " bytes.</div>";

    page += "</div></div></div></body></html>";
    return page;
}
