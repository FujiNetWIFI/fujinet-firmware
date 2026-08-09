#include "clipboardManager.h"

#include <algorithm>

#include <cJSON.h>

#include "../../include/debug.h"
#include "utils.h"

ClipboardManager fnClipboard;

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

    // The clipboard itself takes up index 0, so the history holds one less
    // than the number of addressable snippets.
    while (_history.size() > CLIPBOARD_MAX_SNIPPETS - 1)
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
    // Whatever the browser sent, snippets are stored with LF line endings.
    std::string normalized = text;
    util_replaceAll(normalized, "\r\n", "\n");
    std::replace(normalized.begin(), normalized.end(), '\r', '\n');

    return normalized;
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
    cJSON_AddNumberToObject(root, "max_size", CLIPBOARD_MAX_SIZE);
    cJSON_AddNumberToObject(root, "max_history", CLIPBOARD_MAX_SNIPPETS - 1);

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
