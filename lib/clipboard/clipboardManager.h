/**
 * Clipboard manager
 *
 * Holds a single "current" clipboard entry plus a short history of the
 * snippets that preceded it. Content can be placed on the clipboard by the
 * attached computer (over the bus, via the CLIPBOARD: network protocol) or
 * from the web interface, and read back by either side.
 *
 * Text is stored with LF line endings, whatever the side that wrote it uses:
 * the CLIPBOARD: protocol adapter translates on the way in and out for the
 * computer (see lib/network-protocol/CLIPBOARD.cpp), and the web interface
 * normalizes what the browser posts. Snippets flagged as binary are never
 * translated.
 */

#ifndef CLIPBOARD_MANAGER_H
#define CLIPBOARD_MANAGER_H

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

// Largest snippet we will hold, in bytes.
#define CLIPBOARD_MAX_SIZE (64 * 1024)

// How many snippets are addressable, counting the clipboard itself: indexes
// 0 (the clipboard) through CLIPBOARD_MAX_SNIPPETS - 1 (the oldest).
#define CLIPBOARD_MAX_SNIPPETS 10

// Where a snippet came from.
enum clipboardSource_t : uint8_t
{
    CLIPBOARD_SOURCE_COMPUTER = 0, // written by the computer over the bus
    CLIPBOARD_SOURCE_WEBUI    = 1, // written from the web interface
};

struct ClipboardSnippet
{
    std::string data;
    std::string name;                                  // optional label, e.g. an uploaded file name
    time_t created = 0;                                // wall clock time, 0 if unknown
    clipboardSource_t source = CLIPBOARD_SOURCE_COMPUTER;
    bool binary = false;                               // never translate this snippet
};

class ClipboardManager
{
public:
    // ============ Contents ============

    const ClipboardSnippet &current() const { return _current; }
    bool empty() const { return _current.data.empty(); }
    size_t size() const { return _current.data.size(); }

    // Snippet by index: 0 is the current clipboard, 1..history_count() are the
    // previous snippets, newest first. Returns nullptr if index is out of range.
    const ClipboardSnippet *snippet(size_t index) const;
    size_t history_count() const { return _history.size(); }

    // Replace the clipboard. The outgoing contents, if any, are pushed onto the
    // history. Content longer than CLIPBOARD_MAX_SIZE is truncated.
    void set(std::string data, clipboardSource_t source, bool binary = false,
             const std::string &name = std::string());

    // Make a history snippet current again.
    bool restore(size_t index);

    // Empty the clipboard. The contents are pushed onto the history first, so
    // an accidental clear can still be undone from the web interface.
    void clear();

    // Empty the clipboard and forget every remembered snippet.
    void clear_all();

    // ============ Translation ============

    // Normalize host supplied text (which arrives with whatever line endings
    // the browser sent) to the LF endings snippets are stored with.
    std::string normalize_host_text(const std::string &text) const;

    // ============ Presentation ============

    // A short, printable, JSON safe excerpt of a snippet.
    std::string preview(size_t index, size_t max_len = 120) const;

    // Everything the web interface needs, as a JSON document.
    std::string to_json() const;

private:
    void push_history(ClipboardSnippet &&snippet);

    ClipboardSnippet _current;
    std::deque<ClipboardSnippet> _history; // newest first
};

extern ClipboardManager fnClipboard;

#endif /* CLIPBOARD_MANAGER_H */
