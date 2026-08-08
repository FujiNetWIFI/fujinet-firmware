#include "clipboardManager.h"

#include <algorithm>
#include <cstring>

#include <cJSON.h>

#include "../../include/debug.h"
#include "utils.h"

ClipboardManager fnClipboard;

/**
 * The end of line sequence used by the computer this firmware talks to. Mirrors
 * the defaults each bus's network device assigns to NetworkProtocol::native_eol.
 */
static const char *platform_native_eol()
{
#if defined(BUILD_ATARI)
    return STR_ATASCII_EOL;
#elif defined(BUILD_RS232) || defined(BUILD_RC2014) || defined(BUILD_H89) || defined(BUILD_S100)
    return STR_ASCII_CRLF;
#else
    return STR_ASCII_CR;
#endif
}

/**
 * @brief The line ending a translation mode uses on the host side.
 * @return CR, LF or CR/LF; empty for modes that don't translate line endings.
 */
static const char *host_line_ending(netProtoTranslation_t mode)
{
    switch (mode)
    {
    case NETPROTO_TRANS_CR:   return STR_ASCII_CR;
    case NETPROTO_TRANS_LF:   return STR_ASCII_LF;
    case NETPROTO_TRANS_CRLF: return STR_ASCII_CRLF;
    default:                  return "";
    }
}

ClipboardManager::ClipboardManager()
    : _native_eol(platform_native_eol())
{
}

const ClipboardSnippet *ClipboardManager::snippet(size_t index) const
{
    if (index == 0)
        return &_current;

    if (index > _history.size())
        return nullptr;

    return &_history[index - 1];
}

void ClipboardManager::push_history(ClipboardSnippet &&snippet)
{
    if (snippet.data.empty())
        return;

    _history.push_front(std::move(snippet));

    while (_history.size() > CLIPBOARD_MAX_SNIPPETS)
        _history.pop_back();
}

void ClipboardManager::set(std::string data, clipboardSource_t source, bool binary,
                           const std::string &name)
{
    if (data.size() > CLIPBOARD_MAX_SIZE)
    {
        Debug_printf("Clipboard: truncating %u bytes to %u\n",
                     (unsigned)data.size(), (unsigned)CLIPBOARD_MAX_SIZE);
        data.resize(CLIPBOARD_MAX_SIZE);
    }

    push_history(std::move(_current));

    _current = ClipboardSnippet();
    _current.data = std::move(data);
    _current.name = name;
    _current.created = time(nullptr);
    _current.source = source;
    _current.binary = binary;

    Debug_printf("Clipboard: %u bytes from %s%s\n", (unsigned)_current.data.size(),
                 source == CLIPBOARD_SOURCE_WEBUI ? "web" : "computer",
                 binary ? " (binary)" : "");
}

bool ClipboardManager::restore(size_t index)
{
    if (index == 0 || index > _history.size())
        return false;

    ClipboardSnippet wanted = _history[index - 1];
    _history.erase(_history.begin() + (index - 1));

    push_history(std::move(_current));
    _current = std::move(wanted);

    return true;
}

void ClipboardManager::clear()
{
    push_history(std::move(_current));
    _current = ClipboardSnippet();
    _read_buffer.clear();
    _write_buffer.clear();

    Debug_printf("Clipboard: cleared\n");
}

void ClipboardManager::clear_all()
{
    clear();
    _history.clear();

    Debug_printf("Clipboard: history cleared\n");
}

std::string ClipboardManager::normalize_host_text(const std::string &text) const
{
    const char *eol = host_line_ending(_translation);
    if (!*eol)
        return text;

    // Reduce whatever the host sent to bare LFs, then expand to the mode's
    // line ending.
    std::string normalized = text;
    util_replaceAll(normalized, STR_ASCII_CRLF, STR_ASCII_LF);
    std::replace(normalized.begin(), normalized.end(), '\r', '\n');

    if (strcmp(eol, STR_ASCII_LF) != 0)
        util_replaceAll(normalized, STR_ASCII_LF, eol);

    return normalized;
}

std::string ClipboardManager::snapshot_for_computer(size_t index) const
{
    const ClipboardSnippet *s = snippet(index);
    if (s == nullptr)
        return std::string();

    std::string data = s->data;
    if (!s->binary)
        netproto_translate_to_computer(data, _translation, native_eol());

    return data;
}

void ClipboardManager::set_from_computer(std::string data, bool binary)
{
    if (!binary)
        netproto_translate_from_computer(data, _translation, native_eol());

    set(std::move(data), CLIPBOARD_SOURCE_COMPUTER, binary);
}

bool ClipboardManager::read_take(void *dest, size_t len)
{
    if (len > _read_buffer.size())
        return false;

    memcpy(dest, _read_buffer.data(), len);
    _read_buffer.erase(0, len);
    _read_buffer.shrink_to_fit();

    return true;
}

bool ClipboardManager::write_append(const void *data, size_t len)
{
    if (_write_buffer.size() + len > CLIPBOARD_MAX_SIZE)
    {
        Debug_printf("Clipboard: %u bytes would exceed the %u byte limit\n",
                     (unsigned)(_write_buffer.size() + len), (unsigned)CLIPBOARD_MAX_SIZE);
        return false;
    }

    _write_buffer.append((const char *)data, len);
    return true;
}

std::string ClipboardManager::write_take()
{
    std::string data = std::move(_write_buffer);
    _write_buffer.clear();

    return data;
}

std::string ClipboardManager::preview(size_t index, size_t max_len) const
{
    const ClipboardSnippet *s = snippet(index);
    if (s == nullptr)
        return std::string();

    std::string out;
    out.reserve(max_len);

    for (unsigned char c : s->data)
    {
        if (out.size() >= max_len)
            break;

        // Keep the excerpt printable ASCII so it is safe to drop into JSON
        // whatever the snippet holds.
        if (c == '\r' || c == '\n' || c == 0x9b)
            out += ' ';
        else if (c < 0x20 || c > 0x7e)
            out += '.';
        else
            out += (char)c;
    }

    return out;
}

std::string ClipboardManager::to_json() const
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr)
        return "{}";

    cJSON_AddNumberToObject(root, "size", size());
    cJSON_AddBoolToObject(root, "binary", _current.binary);
    cJSON_AddNumberToObject(root, "translation", _translation);
    cJSON_AddNumberToObject(root, "max_size", CLIPBOARD_MAX_SIZE);
    cJSON_AddNumberToObject(root, "max_snippets", CLIPBOARD_MAX_SNIPPETS);

    cJSON *history = cJSON_AddArrayToObject(root, "history");
    for (size_t i = 1; i <= _history.size(); i++)
    {
        const ClipboardSnippet *s = snippet(i);
        cJSON *entry = cJSON_CreateObject();

        cJSON_AddNumberToObject(entry, "index", i);
        cJSON_AddNumberToObject(entry, "size", s->data.size());
        cJSON_AddBoolToObject(entry, "binary", s->binary);
        cJSON_AddStringToObject(entry, "name", s->name.c_str());
        cJSON_AddNumberToObject(entry, "created", (double)s->created);
        cJSON_AddStringToObject(entry, "source",
                                s->source == CLIPBOARD_SOURCE_WEBUI ? "web" : "computer");
        cJSON_AddStringToObject(entry, "preview", preview(i).c_str());

        cJSON_AddItemToArray(history, entry);
    }

    char *printed = cJSON_PrintUnformatted(root);
    std::string json = printed != nullptr ? printed : "{}";

    cJSON_free(printed);
    cJSON_Delete(root);

    return json;
}
