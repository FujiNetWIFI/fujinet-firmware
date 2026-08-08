/**
 * Clipboard manager
 *
 * Holds a single "current" clipboard entry plus a short history of the
 * snippets that preceded it. Content can be placed on the clipboard by the
 * attached computer (over the bus, via the Fuji device clipboard commands) or
 * from the web interface, and read back by either side.
 *
 * Text is stored in its host-side representation: the line endings the
 * computer uses are folded to the ones selected by the translation mode on the
 * way in, and expanded back again on the way out. This is the same model the
 * Network device uses (see lib/network-protocol/Protocol.cpp) and the
 * translation modes are the same values. Snippets flagged as binary are never
 * translated.
 */

#ifndef CLIPBOARD_MANAGER_H
#define CLIPBOARD_MANAGER_H

#include "../network-protocol/Protocol.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <string>

// Largest snippet we will hold, in bytes.
#define CLIPBOARD_MAX_SIZE (64 * 1024)

// How many previous snippets are remembered.
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
    ClipboardManager();

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

    netProtoTranslation_t translation() const { return _translation; }
    void set_translation(netProtoTranslation_t mode) { _translation = mode; }

    // The attached computer's native end of line sequence.
    const std::string &native_eol() const { return _native_eol; }
    void set_native_eol(const std::string &eol) { _native_eol = eol; }

    // Normalize host supplied text (which arrives with whatever line endings
    // the browser sent) to the line ending the current translation mode uses.
    std::string normalize_host_text(const std::string &text) const;

    // ============ Bus side staging ============
    // Reads and writes over the bus are chunked, so a transfer in either
    // direction is staged here until it completes. Character set conversion is
    // the caller's job, since only the device knows which bus it is talking to.

    // A copy of a snippet with its line endings translated for the computer.
    std::string snapshot_for_computer(size_t index) const;
    // Translate content received from the computer and make it the clipboard.
    void set_from_computer(std::string data, bool binary);

    void read_stage(std::string data) { _read_buffer = std::move(data); }
    size_t read_available() const { return _read_buffer.size(); }
    // Consume len bytes from the front of the staged read.
    bool read_take(void *dest, size_t len);

    // Append to the staged write. Fails if the result would be over the limit.
    bool write_append(const void *data, size_t len);
    size_t write_pending() const { return _write_buffer.size(); }
    void write_abort() { _write_buffer.clear(); }
    // Hand over the staged write, leaving the staging buffer empty.
    std::string write_take();

    // ============ Presentation ============

    // A short, printable, JSON safe excerpt of a snippet.
    std::string preview(size_t index, size_t max_len = 120) const;

    // Everything the web interface needs, as a JSON document.
    std::string to_json() const;

private:
    void push_history(ClipboardSnippet &&snippet);

    ClipboardSnippet _current;
    std::deque<ClipboardSnippet> _history; // newest first

    netProtoTranslation_t _translation = NETPROTO_TRANS_LF;
    std::string _native_eol;

    std::string _read_buffer;
    std::string _write_buffer;
};

extern ClipboardManager fnClipboard;

#endif /* CLIPBOARD_MANAGER_H */
