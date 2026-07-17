/**
 * NetworkProtocolMailbox - base class for mailbox (e-mail) protocol adapters.
 */

#include "Mailbox.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>

#include "../../include/debug.h"
#include "status_error_codes.h"

// The human-readable index/count lines are terminated with `lineEnding`, the
// per-device end-of-line set by the network.cpp layer (via setLineEnding(),
// like the JSON parser). Defaults to 0x9B (Atari) in the NetworkProtocol base.

// Default number of messages returned when range= is absent.
#define MB_DEFAULT_RANGE 20
// Upper bound on messages fetched for one index, to bound work and memory.
#define MB_MAX_RANGE 200

// ─── small string/byte helpers ────────────────────────────────────────────────

namespace {

std::string ellipsize(const std::string &s, int w)
{
    if (w <= 0) return "";
    if ((int)s.size() <= w) return s;
    if (w <= 3) return s.substr(0, w);
    return s.substr(0, w - 3) + "...";
}

std::string ljust(const std::string &s, int w)
{
    if ((int)s.size() >= w) return s.substr(0, w);
    return s + std::string(w - s.size(), ' ');
}

std::string rjust(const std::string &s, int w)
{
    if ((int)s.size() >= w) return s.substr(0, w);
    return std::string(w - s.size(), ' ') + s;
}

// Append `n` little-endian bytes of `v`.
void append_le(std::string &b, uint64_t v, int n)
{
    for (int i = 0; i < n; i++)
    {
        b += (char)(v & 0xFF);
        v >>= 8;
    }
}

// Append a fixed-width, NUL-terminated, NUL-padded char field.
void append_fixed(std::string &b, const std::string &s, size_t n)
{
    size_t c = (n > 0) ? std::min(s.size(), n - 1) : 0;
    b.append(s.data(), c);
    b.append(n - c, '\0');
}

std::string humanize_date(uint64_t ts)
{
    time_t t = (time_t)ts;
    time_t now = time(nullptr);
    struct tm tmv;
    struct tm nowv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
    localtime_s(&nowv, &now);
#else
    localtime_r(&t, &tmv);
    localtime_r(&now, &nowv);
#endif

    char buf[16];
    if (tmv.tm_year == nowv.tm_year)
        strftime(buf, sizeof(buf), "%d %b", &tmv); // e.g. "31 Jul"
    else
        strftime(buf, sizeof(buf), "%m/%d/%y", &tmv); // e.g. "07/31/23"
    return buf;
}

std::string humanize_size(uint64_t n)
{
    char buf[16];
    if (n < 1024ULL)
        snprintf(buf, sizeof(buf), "%uB", (unsigned)n);
    else if (n < 1024ULL * 1024)
        snprintf(buf, sizeof(buf), "%.1fK", n / 1024.0);
    else if (n < 1024ULL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1fM", n / (1024.0 * 1024));
    else
        snprintf(buf, sizeof(buf), "%.1fG", n / (1024.0 * 1024 * 1024));
    return buf;
}

} // namespace

// ─── construction ─────────────────────────────────────────────────────────────

NetworkProtocolMailbox::NetworkProtocolMailbox(std::string *rx_buf, std::string *tx_buf,
                                               std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolMailbox::ctor\r\n");

    // Per-platform default human-readable line width (used when the width
    // parameter is 0). Fallback 40; MS-DOS maps to the RS232 platform.
#if defined(BUILD_ATARI)
    _defaultWidth = 38;
#elif defined(BUILD_APPLE)
    _defaultWidth = 40;
#elif defined(BUILD_ADAM)
    _defaultWidth = 32;
#elif defined(BUILD_COCO)
    _defaultWidth = 32;
#elif defined(BUILD_RS232)
    _defaultWidth = 80;
#else
    _defaultWidth = 40;
#endif
}

NetworkProtocolMailbox::~NetworkProtocolMailbox()
{
    Debug_printf("NetworkProtocolMailbox::dtor\r\n");
}

// ─── path parsing ─────────────────────────────────────────────────────────────

std::vector<std::string> NetworkProtocolMailbox::split_path(const std::string &path)
{
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/'))
        if (!part.empty())
            parts.push_back(part);
    return parts;
}

// ─── open / dispatch ──────────────────────────────────────────────────────────

fujiError_t NetworkProtocolMailbox::open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                                         netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);
    error = NDEV_STATUS::SUCCESS;
    receiveBuffer->clear();

    bool isDir = (access == ACCESS_MODE::DIRECTORY || access == ACCESS_MODE::DIRECTORY_ALT);
    bool isRead = (access == ACCESS_MODE::READ);
    if (!isDir && !isRead)
    {
        // Mailboxes are read-only.
        error = NDEV_STATUS::READ_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (connect_and_auth() != FUJI_ERROR::NONE)
    {
        mailbox_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::vector<std::string> parts = split_path(urlParser->path);
    fujiError_t res;

    switch (parts.size())
    {
    case 1: // /FOLDER
        _folder = parts[0];
        res = isDir ? do_folder_index((uint8_t)translate) : do_folder_count();
        break;
    case 2: // /FOLDER/N
        _folder = parts[0];
        _seq = (uint32_t)strtoul(parts[1].c_str(), nullptr, 10);
        res = isDir ? do_attachment_index((uint8_t)translate) : do_message_body();
        break;
    case 3: // /FOLDER/N/A
        _folder = parts[0];
        _seq = (uint32_t)strtoul(parts[1].c_str(), nullptr, 10);
        _attach = (uint8_t)strtoul(parts[2].c_str(), nullptr, 10);
        if (isDir)
        {
            error = NDEV_STATUS::INVALID_DEVICESPEC;
            res = FUJI_ERROR::UNSPECIFIED;
        }
        else
            res = do_attachment_data();
        break;
    default:
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        res = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    forceStatus = true;
    return res;
}

fujiError_t NetworkProtocolMailbox::do_folder_count()
{
    uint32_t count = 0;
    if (folder_count(_folder, count) != FUJI_ERROR::NONE)
    {
        mailbox_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }
    *receiveBuffer = std::to_string(count) + lineEnding;
    translation_mode = NETPROTO_TRANS_NONE; // already terminated with native EOL
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolMailbox::do_folder_index(uint8_t transByte)
{
    // aux2 == 255 selects raw binary; anything else is human-readable, with the
    // line width taken from the low 7 bits (0 -> default). NOS sends 128 for a
    // default directory listing -> width 0 -> human-readable at the default width.
    bool raw = (transByte == 0xFF);
    int width = transByte & 0x7F;
    if (width == 0) width = _defaultWidth; // platform default when unspecified

    long rangeStart = 0;
    long rangeEnd = MB_DEFAULT_RANGE - 1;
    std::string range = opened_url->queryParam("range");
    if (!range.empty())
    {
        size_t dash = range.find('-');
        if (dash != std::string::npos)
        {
            rangeStart = strtol(range.substr(0, dash).c_str(), nullptr, 10);
            rangeEnd = strtol(range.substr(dash + 1).c_str(), nullptr, 10);
        }
        else
            rangeEnd = strtol(range.c_str(), nullptr, 10);
    }
    if (rangeStart < 0) rangeStart = 0;
    if (rangeEnd < rangeStart) rangeEnd = rangeStart;
    if (rangeEnd - rangeStart + 1 > MB_MAX_RANGE) rangeEnd = rangeStart + MB_MAX_RANGE - 1;

    bool newest = opened_url->queryParam("newest", "1") != "0";

    std::vector<MailboxIndexEntry> items;
    if (folder_index(_folder, rangeStart, rangeEnd, newest, items) != FUJI_ERROR::NONE)
    {
        mailbox_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (raw)
        format_index_raw(items);
    else
        format_index_human(items, width);

    translation_mode = NETPROTO_TRANS_NONE;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolMailbox::do_message_body()
{
    std::string body;
    if (message_body(_folder, _seq, body) != FUJI_ERROR::NONE)
    {
        mailbox_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }
    *receiveBuffer = body;
    translate_receive_buffer(); // honor the aux2 EOL translation once
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolMailbox::do_attachment_index(uint8_t transByte)
{
    // aux2 == 255 selects raw binary; anything else is human-readable, with the
    // line width taken from the low 7 bits (0 -> default). NOS sends 128 for a
    // default directory listing -> width 0 -> human-readable at the default width.
    bool raw = (transByte == 0xFF);
    int width = transByte & 0x7F;
    if (width == 0) width = _defaultWidth; // platform default when unspecified

    std::vector<MailboxAttachmentEntry> items;
    if (attachment_index(_folder, _seq, items) != FUJI_ERROR::NONE)
    {
        mailbox_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (raw)
        format_attachment_index_raw(items);
    else
        format_attachment_index_human(items, width);

    translation_mode = NETPROTO_TRANS_NONE;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolMailbox::do_attachment_data()
{
    std::string data;
    if (attachment_data(_folder, _seq, _attach, data) != FUJI_ERROR::NONE)
    {
        mailbox_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }
    *receiveBuffer = data;
    translate_receive_buffer();
    return FUJI_ERROR::NONE;
}

// ─── formatting ───────────────────────────────────────────────────────────────

void NetworkProtocolMailbox::format_index_human(const std::vector<MailboxIndexEntry> &items, int width)
{
    // Two-line-per-item layout tuned for a 40-column Atari display, whose default
    // margins make a printed line wrap back to the left after 38 characters. Each
    // item is one logical record (single trailing EOL) built as two 38-column
    // halves: line 1 is padded to exactly lineW so the auto-wrap lands on the half
    // boundary. It wraps to two lines on a 40-column screen and reads as one line
    // on an 80-column screen.
    int lineW = (width >= 12) ? width : _defaultWidth;

    uint32_t maxNum = 1;
    for (auto &it : items)
        if (it.msgNum > maxNum) maxNum = it.msgNum;
    int numW = (int)std::to_string(maxNum).size();

    const int dateW = 8; // fits "07/31/23" or "31 Jul"
    int nameW = lineW - numW - dateW - 4; // flag + 3 separators
    if (nameW < 3) nameW = 3;
    const int subjIndent = 2;
    int subjW = lineW - subjIndent;
    if (subjW < 3) subjW = 3;

    std::string out;

    // Header mirrors the line-1 columns, dashed, exactly lineW wide.
    {
        std::string h = "I ";
        h += rjust("#", numW);
        h += ' ';
        h += ljust("Name", nameW);
        h += ' ';
        h += rjust("Date", dateW);
        for (auto &c : h)
            if (c == ' ') c = '-';
        out += h;
        out += lineEnding;
    }

    for (auto &it : items)
    {
        std::string name = it.displayName.empty() ? it.emailAddress : it.displayName;

        // Line 1 padded to exactly lineW so the 40-col wrap lands at the half.
        std::string l1;
        l1 += it.important ? '*' : ' ';
        l1 += ' ';
        l1 += rjust(std::to_string(it.msgNum), numW);
        l1 += ' ';
        l1 += ljust(ellipsize(name, nameW), nameW);
        l1 += ' ';
        l1 += rjust(humanize_date(it.timestamp), dateW);
        out += ljust(l1, lineW);

        // Line 2 (the wrapped half): subject, small indent, no trailing pad.
        out += std::string(subjIndent, ' ');
        out += ellipsize(it.subject, subjW);
        out += lineEnding;
    }

    *receiveBuffer = out;
}

void NetworkProtocolMailbox::format_index_raw(const std::vector<MailboxIndexEntry> &items)
{
    std::string out;
    for (auto &it : items)
    {
        append_le(out, it.msgNum, 4);
        append_fixed(out, it.displayName, 32);
        append_fixed(out, it.emailAddress, 48);
        append_fixed(out, it.subject, 128);
        append_le(out, it.timestamp, 8);
    }
    *receiveBuffer = out;
}

void NetworkProtocolMailbox::format_attachment_index_human(const std::vector<MailboxAttachmentEntry> &items,
                                                           int width)
{
    if (width <= 0) width = _defaultWidth;

    uint8_t maxNum = 0;
    for (auto &it : items)
        if (it.attachmentNum > maxNum) maxNum = it.attachmentNum;
    int numW = (int)std::to_string((unsigned)maxNum).size();

    const int sizeW = 8;
    int fixedLeft = numW + 1;
    int remaining = width - fixedLeft - sizeW - 1;
    if (remaining < 6) remaining = 6;
    int nameW = remaining / 2;
    if (nameW < 3) nameW = 3;
    int mimeW = remaining - nameW - 1;
    if (mimeW < 3) mimeW = 3;

    std::string out;
    out += rjust("#", numW);
    out += ' ';
    out += ljust("Name", nameW);
    out += ' ';
    out += ljust("Type", mimeW);
    out += ' ';
    out += rjust("Size", sizeW);
    out += lineEnding;

    for (auto &it : items)
    {
        std::string name = it.fileName.empty() ? it.displayName : it.fileName;
        out += rjust(std::to_string((unsigned)it.attachmentNum), numW);
        out += ' ';
        out += ljust(ellipsize(name, nameW), nameW);
        out += ' ';
        out += ljust(ellipsize(it.mimeType, mimeW), mimeW);
        out += ' ';
        out += rjust(humanize_size(it.length), sizeW);
        out += lineEnding;
    }

    *receiveBuffer = out;
}

void NetworkProtocolMailbox::format_attachment_index_raw(const std::vector<MailboxAttachmentEntry> &items)
{
    std::string out;
    for (auto &it : items)
    {
        out += (char)it.attachmentNum;
        append_fixed(out, it.displayName, 128);
        append_fixed(out, it.fileName, 128);
        append_fixed(out, it.mimeType, 48);
        append_le(out, it.length, 8);
    }
    *receiveBuffer = out;
}

// ─── read / status / available ────────────────────────────────────────────────

fujiError_t NetworkProtocolMailbox::read(unsigned short len)
{
    // All content is staged into receiveBuffer at open(); the device drains it.
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolMailbox::status(NetworkStatus *status)
{
    if (error == NDEV_STATUS::SUCCESS && receiveBuffer->empty())
        status->error = NDEV_STATUS::END_OF_FILE;
    else
        status->error = error;
    status->connected = receiveBuffer->empty() ? 0 : 1;
    return FUJI_ERROR::NONE;
}

size_t NetworkProtocolMailbox::available()
{
    return receiveBuffer->size();
}
