/**
 * Pretty renderer for #FujiNet
 */

#include "fnpretty.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "gumbo.h"

#include "../network-protocol/Protocol.h"

#include "../../include/debug.h"

namespace {

// Cap on the document we will buffer+parse; page size is attacker-controlled.
constexpr size_t kMaxBodyBytes = 512 * 1024;

// Per-read drain size. Must stay <= 65535 because NetworkProtocol::read() takes
// an unsigned short and larger values truncate (a multiple of 65536 -> 0).
constexpr size_t kReadChunkBytes = 32768;

// Narrowest width we will wrap to; below this nearly every word hard-breaks.
constexpr uint8_t kMinScreenWidth = 16;

/**
 * Elements whose contents are not page text at all.
 */
bool isSkippedTag(GumboTag tag)
{
    switch (tag)
    {
    case GUMBO_TAG_HEAD:
    case GUMBO_TAG_SCRIPT:
    case GUMBO_TAG_STYLE:
    case GUMBO_TAG_NOSCRIPT:
    case GUMBO_TAG_TEMPLATE:
    case GUMBO_TAG_IFRAME:
    case GUMBO_TAG_SVG:
    case GUMBO_TAG_MATH:
        return true;
    default:
        return false;
    }
}

/**
 * Elements that start and end a line of output. Everything else is inline and
 * its text is run together with its siblings'.
 */
bool isBlockTag(GumboTag tag)
{
    switch (tag)
    {
    case GUMBO_TAG_ADDRESS:
    case GUMBO_TAG_ARTICLE:
    case GUMBO_TAG_ASIDE:
    case GUMBO_TAG_BLOCKQUOTE:
    case GUMBO_TAG_BODY:
    case GUMBO_TAG_CAPTION:
    case GUMBO_TAG_CENTER:
    case GUMBO_TAG_DD:
    case GUMBO_TAG_DETAILS:
    case GUMBO_TAG_DIV:
    case GUMBO_TAG_DL:
    case GUMBO_TAG_DT:
    case GUMBO_TAG_FIELDSET:
    case GUMBO_TAG_FIGCAPTION:
    case GUMBO_TAG_FIGURE:
    case GUMBO_TAG_FOOTER:
    case GUMBO_TAG_FORM:
    case GUMBO_TAG_H1:
    case GUMBO_TAG_H2:
    case GUMBO_TAG_H3:
    case GUMBO_TAG_H4:
    case GUMBO_TAG_H5:
    case GUMBO_TAG_H6:
    case GUMBO_TAG_HEADER:
    case GUMBO_TAG_HR:
    case GUMBO_TAG_HTML:
    case GUMBO_TAG_LEGEND:
    case GUMBO_TAG_LI:
    case GUMBO_TAG_MAIN:
    case GUMBO_TAG_NAV:
    case GUMBO_TAG_OL:
    case GUMBO_TAG_P:
    case GUMBO_TAG_PRE:
    case GUMBO_TAG_SECTION:
    case GUMBO_TAG_SUMMARY:
    case GUMBO_TAG_TABLE:
    case GUMBO_TAG_TBODY:
    case GUMBO_TAG_TD:
    case GUMBO_TAG_TFOOT:
    case GUMBO_TAG_TH:
    case GUMBO_TAG_THEAD:
    case GUMBO_TAG_TR:
    case GUMBO_TAG_UL:
        return true;
    default:
        return false;
    }
}

} // namespace

FNPretty::FNPretty()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNPretty::ctor()\r\n");
#endif
}

FNPretty::~FNPretty()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNPretty::dtor()\r\n");
#endif
    _protocol = nullptr;
}

void FNPretty::setLineEnding(const std::string &_lineEnding)
{
    lineEnding = _lineEnding;
}

void FNPretty::setProtocol(NetworkProtocol *newProtocol)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNPretty::setProtocol()\r\n");
#endif
    _protocol = newProtocol;
}

void FNPretty::setScreenWidth(uint8_t width)
{
    if (width == 0) // no width given, keep what we have
        return;

    _screenWidth = width < kMinScreenWidth ? kMinScreenWidth : width;
#ifdef VERBOSE_PROTOCOL
    Debug_printf("FNPretty::setScreenWidth(%u)\r\n", _screenWidth);
#endif
}

/**
 * Drain the entire body from the protocol. Mirrors FNSGML::fetch semantics:
 * bounded chunks, a hard cap on total size, and a stall check so a dead
 * connection cannot spin here forever.
 */
bool FNPretty::fetch()
{
    NetworkStatus ns;

    if (_protocol == nullptr)
        return false;

    _parseBuffer.clear();
    _protocol->status(&ns);
    while (ns.connected || _protocol->available() > 0)
    {
        size_t avail = _protocol->available();
        if (avail > 0)
        {
            // NetworkProtocol::read() takes unsigned short, so a raw available()
            // >= 65536 truncates (e.g. 65536 -> 0) and stalls the drain forever.
            unsigned short chunk = avail > kReadChunkBytes ? (unsigned short)kReadChunkBytes
                                                           : (unsigned short)avail;
            _protocol->read(chunk);
            if (_protocol->receiveBuffer->empty())
                break; // no forward progress (EOF/stalled) - stop draining
            _parseBuffer += *_protocol->receiveBuffer;
            _protocol->receiveBuffer->clear();
            if (_parseBuffer.size() > kMaxBodyBytes)
            {
#ifdef VERBOSE_PROTOCOL
                Debug_printf("FNPretty::fetch() - body exceeds %zu bytes, truncating\r\n", kMaxBodyBytes);
#endif
                _parseBuffer.resize(kMaxBodyBytes);
                break;
            }
        }
        _protocol->status(&ns);
#ifdef ESP_PLATFORM
        vTaskDelay(10);
#endif
    }

    return !_parseBuffer.empty();
}

bool FNPretty::parse()
{
    if (!fetch())
    {
        _rendered.clear();
        _pending.clear();
        _readPos = 0;
        return false;
    }

    bool ok = renderDocument(_parseBuffer);

    // The tree is gone; drop the body so the page isn't held twice.
    _parseBuffer.clear();
    _parseBuffer.shrink_to_fit();

    return ok;
}

/**
 * Render a fetched document. HTML is all we know how to render today; when a
 * second format arrives, pick the renderer here.
 */
bool FNPretty::renderDocument(const std::string &body)
{
    _rendered.clear();
    _pending.clear();
    _readPos = 0;

    return renderHtml(body);
}

bool FNPretty::renderHtml(const std::string &body)
{
    // Gumbo does full HTML5 error-recovery, so a raw (malformed) body is fine.
    GumboOutput *output = gumbo_parse_with_options(&kGumboDefaultOptions,
                                                  body.data(), body.size());
    if (output == nullptr)
        return false;

    renderHtmlNode(output->root);
    flushBlock(); // anything left over after the last block

    gumbo_destroy_output(&kGumboDefaultOptions, output);

    Debug_printf("FNPretty::renderHtml() - rendered %zu bytes at %u columns\r\n",
                 _rendered.size(), _screenWidth);
    return true;
}

/**
 * Walk the tree, accumulating inline text and breaking lines at block edges.
 *
 * For now every block renders the same way: its text, wrapped, on its own
 * line(s). Per-tag treatment (h1 underlines, "* " bullets for li, indented
 * blockquotes, table columns) hangs off the isBlockTag() switch above and the
 * tag switch below when we get to it.
 */
void FNPretty::renderHtmlNode(const GumboNode *node)
{
    if (node == nullptr)
        return;

    switch (node->type)
    {
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_WHITESPACE:
    case GUMBO_NODE_CDATA:
        appendInline(node->v.text.text);
        return;
    case GUMBO_NODE_ELEMENT:
    case GUMBO_NODE_TEMPLATE:
        break;
    default: // comments, doctype
        return;
    }

    GumboTag tag = node->v.element.tag;
    if (isSkippedTag(tag))
        return;

    if (tag == GUMBO_TAG_BR)
    {
        flushBlock();
        return;
    }

    bool block = isBlockTag(tag);
    if (block)
        flushBlock();

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));

    if (block)
        flushBlock();
}

/**
 * Add text to the current block, collapsing runs of whitespace the way a
 * browser would.
 */
void FNPretty::appendInline(const char *text)
{
    if (text == nullptr)
        return;

    for (const char *p = text; *p != '\0'; p++)
    {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
        {
            if (!_pending.empty() && _pending.back() != ' ')
                _pending.push_back(' ');
        }
        else
        {
            _pending.push_back(c);
        }
    }
}

/**
 * Emit the current block's text as wrapped line(s). Blocks that hold no text
 * produce no output at all, so the markup's nesting doesn't turn into a stack
 * of blank lines.
 */
void FNPretty::flushBlock()
{
    size_t start = _pending.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        _pending.clear();
        return;
    }

    size_t end = _pending.find_last_not_of(' ');
    emitWrapped(_pending.substr(start, end - start + 1));
    _pending.clear();
}

/**
 * Word-wrap text to the screen width, breaking mid-word only for words that
 * are too long to fit a line on their own.
 *
 * Widths are counted in bytes, so a multi-byte UTF-8 sequence counts as more
 * than the one column it occupies - fine for the (mostly ASCII) pages an 8-bit
 * client is reading, and where remapping matters the client asks for it.
 */
void FNPretty::emitWrapped(const std::string &text)
{
    const size_t width = _screenWidth;
    size_t pos = 0;

    while (pos < text.size())
    {
        if (text.size() - pos <= width)
        {
            emitLine(text.substr(pos));
            return;
        }

        // rfind() is inclusive of the start index, so a space landing exactly
        // on the column after the last one that fits is a clean break.
        size_t brk = text.rfind(' ', pos + width);
        if (brk == std::string::npos || brk <= pos)
        {
            emitLine(text.substr(pos, width)); // word longer than the screen
            pos += width;
        }
        else
        {
            emitLine(text.substr(pos, brk - pos));
            pos = brk + 1;
        }
    }
}

void FNPretty::emitLine(const std::string &line)
{
    _rendered += line;
    _rendered += lineEnding;
}

size_t FNPretty::readValue(uint8_t *buf, size_t len)
{
    size_t avail = available();
    size_t n = len < avail ? len : avail;

    if (n > 0)
    {
        memcpy(buf, _rendered.data() + _readPos, n);
        _readPos += n;
    }

    return n;
}

bool FNPretty::status(NetworkStatus *s)
{
    s->connected = available() > 0;
    s->error = available() == 0 ? NDEV_STATUS::END_OF_FILE : NDEV_STATUS::SUCCESS;
    return false;
}
