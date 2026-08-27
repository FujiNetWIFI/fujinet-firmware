/**
 * Pretty renderer for #FujiNet
 */

#include "fnpretty.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "gumbo.h"

#include "Document.h"
#include "Node.h"
#include "Selection.h"

#include "fn_sanitize.h"

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

// Indent added per list nesting level, ahead of the marker itself.
constexpr size_t kListIndent = 2;

// Columns of text we always keep to the right of the indent; nesting stops
// deepening rather than squeezing content down to a letter per line.
constexpr size_t kMinTextWidth = 8;

// At or above this width a table is drawn with full outer borders; below it
// the left/right borders are dropped, buying back 4 columns of content on
// 32- and 40-column displays where that matters most.
constexpr uint8_t kOuterBorderMinWidth = 80;

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
 * Elements that start and end a line of output. Tags handled specially by
 * renderHtmlNode() (p, hr, blockquote, pre, dl/dt/dd, h1-h6, ul/ol/li, table)
 * are pulled out of this set at dispatch time; everything else in it falls
 * through to a plain margin-0/0 "generic container" (just a line break, as
 * blocks have always done). Anything not in this set is inline and runs
 * together with its siblings' text.
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

// Bound on how deep renderTable()'s layout-vs-data classification will
// recurse (through tbody/thead/tfoot wrappers, and down into a cell looking
// for a nested <table>), so a pathologically deep/malformed document cannot
// make classification recurse without bound.
constexpr int kMaxTableClassifyDepth = 32;

/**
 * True if a <table> anywhere in node's subtree - used to decide whether a
 * cell of the table being classified contains a nested table.
 */
bool cellContainsTable(const GumboNode *node, int depth)
{
    if (node == nullptr || depth <= 0)
        return false;
    if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
        return false;
    if (node->v.element.tag == GUMBO_TAG_TABLE)
        return true;

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
    {
        if (cellContainsTable(static_cast<const GumboNode *>(children->data[i]), depth - 1))
            return true;
    }
    return false;
}

/**
 * Walk node's rows the same way collectTableRows() does (through any
 * tbody/thead/tfoot wrapper, not descending into a nested <table>'s own
 * rows), counting this table's own cells and checking each one for a nested
 * <table> anywhere below it. Deliberately does not call captureInline() -
 * classification only needs cell counts and a yes/no nested-table check, so
 * touching _pending here would be wasted work and an unwanted side effect.
 */
void classifyTableWalk(const GumboNode *node, size_t &cellCount, bool &hasNestedTable, int depth)
{
    if (node == nullptr || depth <= 0 || hasNestedTable)
        return;
    if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
        return;

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length && !hasNestedTable; i++)
    {
        const GumboNode *child = static_cast<const GumboNode *>(children->data[i]);
        if (child->type != GUMBO_NODE_ELEMENT)
            continue;

        GumboTag tag = child->v.element.tag;
        if (tag == GUMBO_TAG_TABLE)
            continue; // a nested table's rows are its own, not this scan's

        if (tag == GUMBO_TAG_TR)
        {
            const GumboVector *cells = &child->v.element.children;
            for (unsigned int j = 0; j < cells->length && !hasNestedTable; j++)
            {
                const GumboNode *cell = static_cast<const GumboNode *>(cells->data[j]);
                if (cell->type != GUMBO_NODE_ELEMENT)
                    continue;
                GumboTag ctag = cell->v.element.tag;
                if (ctag != GUMBO_TAG_TD && ctag != GUMBO_TAG_TH)
                    continue;
                cellCount++;
                if (cellContainsTable(cell, depth - 1))
                    hasNestedTable = true;
            }
        }
        else
        {
            classifyTableWalk(child, cellCount, hasNestedTable, depth - 1);
        }
    }
}

/**
 * 2000s-era sites routinely use a <table> purely for page layout rather than
 * tabular data, nested several deep (a one-cell wrapper containing the "real"
 * content table, itself sometimes containing more layout tables). Drawing a
 * grid around every level of that produces a handful of giant boxes of
 * run-together text instead of a readable page.
 *
 * Heuristic: a table is layout, not data, if it has one cell or fewer in
 * total (nothing to tabulate, or a single wrapper cell), or if any of its
 * cells contains a descendant <table> (a data table's cells hold data, not
 * more markup for a whole other table). A layout table is rendered
 * transparently - as ordinary blocks in document order - rather than as a
 * grid; renderTable() recurses through it via the plain isBlockTag() path,
 * so a nested table is re-classified by this same predicate on the way down.
 *
 * The one trade-off: a genuine data table whose cell happens to embed a
 * nested table (e.g. a sortable widget dropped into one cell) degrades to
 * linear text instead of a grid. That's an acceptable price for making
 * layout-table sites readable at all.
 */
bool isLayoutTable(const GumboNode *table)
{
    size_t cellCount = 0;
    bool hasNestedTable = false;
    classifyTableWalk(table, cellCount, hasNestedTable, kMaxTableClassifyDepth);
    return cellCount <= 1 || hasNestedTable;
}

std::string spaces(size_t n)
{
    return std::string(n, ' ');
}

std::string trimAscii(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n\f\v");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n\f\v");
    return s.substr(start, end - start + 1);
}

std::string getAttribute(const GumboNode *node, const char *name)
{
    if (node == nullptr || (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE))
        return "";

    GumboAttribute *attr = gumbo_get_attribute(&node->v.element.attributes, name);
    return attr != nullptr ? attr->value : "";
}

/**
 * Word-wrap text to width columns, breaking on the last space that fits and
 * hard-breaking words too long to fit a line on their own. Returns no lines
 * at all for empty text.
 *
 * Widths are counted in bytes, so a multi-byte UTF-8 sequence counts as more
 * than the one column it occupies - fine for the (mostly ASCII) pages an
 * 8-bit client is reading, and where remapping matters the client asks for
 * it.
 */
std::vector<std::string> wrapText(const std::string &text, size_t width)
{
    std::vector<std::string> lines;
    if (width == 0)
        width = 1;

    size_t pos = 0;
    while (pos < text.size())
    {
        if (text.size() - pos <= width)
        {
            lines.push_back(text.substr(pos));
            break;
        }

        // rfind() is inclusive of the start index, so a space landing exactly
        // on the column after the last one that fits is a clean break.
        size_t brk = text.rfind(' ', pos + width);
        if (brk == std::string::npos || brk <= pos)
        {
            lines.push_back(text.substr(pos, width)); // word longer than the screen
            pos += width;
        }
        else
        {
            lines.push_back(text.substr(pos, brk - pos));
            pos = brk + 1;
        }
    }
    return lines;
}

/**
 * Expand tabs to 8-column stops.
 */
std::string expandTabs(const std::string &s)
{
    std::string out;
    size_t col = 0;
    for (char c : s)
    {
        if (c == '\t')
        {
            size_t n = 8 - (col % 8);
            out.append(n, ' ');
            col += n;
        }
        else
        {
            out.push_back(c);
            col++;
        }
    }
    return out;
}

std::vector<std::string> splitLines(const std::string &s)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (true)
    {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos)
        {
            lines.push_back(s.substr(pos));
            break;
        }
        lines.push_back(s.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

bool hasScheme(const std::string &s)
{
    if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0])))
        return false;

    size_t i = 1;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '+' || s[i] == '-' || s[i] == '.'))
        i++;

    return i < s.size() && s[i] == ':';
}

/**
 * RFC 3986 5.2.4 remove_dot_segments, operating directly on the path string.
 */
std::string removeDotSegments(std::string input)
{
    std::string output;
    while (!input.empty())
    {
        if (input.compare(0, 3, "../") == 0)
        {
            input.erase(0, 3);
        }
        else if (input.compare(0, 2, "./") == 0)
        {
            input.erase(0, 2);
        }
        else if (input.compare(0, 3, "/./") == 0)
        {
            input.replace(0, 3, "/");
        }
        else if (input == "/.")
        {
            input = "/";
        }
        else if (input.compare(0, 4, "/../") == 0)
        {
            input.replace(0, 4, "/");
            size_t slash = output.find_last_of('/');
            output.erase(slash == std::string::npos ? 0 : slash);
        }
        else if (input == "/..")
        {
            input = "/";
            size_t slash = output.find_last_of('/');
            output.erase(slash == std::string::npos ? 0 : slash);
        }
        else if (input == "." || input == "..")
        {
            input.clear();
        }
        else
        {
            // Move the first path segment (including any leading '/') from
            // input to output verbatim.
            size_t start = (input[0] == '/') ? 1 : 0;
            size_t nextSlash = input.find('/', start);
            if (nextSlash == std::string::npos)
            {
                output += input;
                input.clear();
            }
            else
            {
                output += input.substr(0, nextSlash);
                input.erase(0, nextSlash);
            }
        }
    }
    return output;
}

/**
 * Simplified RFC 3986 5.3 URL resolution - enough for real pages' relative
 * hrefs, not a full implementation (no userinfo/port edge cases beyond
 * treating them as part of the opaque "authority").
 */
std::string resolveUrl(const std::string &base, const std::string &href)
{
    if (hasScheme(href))
        return href;
    if (base.empty())
        return href;

    size_t schemeSep = base.find("://");
    if (schemeSep == std::string::npos)
        return href;

    std::string schemeColon = base.substr(0, schemeSep + 1);   // e.g. "https:"
    std::string schemeSlashes = base.substr(0, schemeSep + 3); // e.g. "https://"

    size_t authStart = schemeSep + 3;
    size_t authEnd = base.find_first_of("/?#", authStart);
    std::string authority = (authEnd == std::string::npos) ? base.substr(authStart)
                                                             : base.substr(authStart, authEnd - authStart);

    std::string basePath;
    if (authEnd != std::string::npos && base[authEnd] == '/')
    {
        size_t pathEnd = base.find_first_of("?#", authEnd);
        basePath = (pathEnd == std::string::npos) ? base.substr(authEnd) : base.substr(authEnd, pathEnd - authEnd);
    }

    if (href.compare(0, 2, "//") == 0)
        return schemeColon + href;

    if (!href.empty() && href[0] == '/')
    {
        size_t tailPos = href.find_first_of("?#");
        std::string hrefPath = (tailPos == std::string::npos) ? href : href.substr(0, tailPos);
        std::string hrefTail = (tailPos == std::string::npos) ? "" : href.substr(tailPos);
        return schemeSlashes + authority + removeDotSegments(hrefPath) + hrefTail;
    }

    if (!href.empty() && (href[0] == '?' || href[0] == '#'))
        return schemeSlashes + authority + basePath + href;

    // Merge: base path up to and including its last '/' (or "/" if none).
    size_t lastSlash = basePath.find_last_of('/');
    std::string dir = (lastSlash == std::string::npos) ? "/" : basePath.substr(0, lastSlash + 1);

    size_t tailPos = href.find_first_of("?#");
    std::string hrefPath = (tailPos == std::string::npos) ? href : href.substr(0, tailPos);
    std::string hrefTail = (tailPos == std::string::npos) ? "" : href.substr(tailPos);

    return schemeSlashes + authority + removeDotSegments(dir + hrefPath) + hrefTail;
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
    if (_doc != nullptr)
        delete _doc;
    _doc = nullptr;
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

void FNPretty::setBaseUrl(const std::string &url)
{
    _baseUrl = url;
}

void FNPretty::setFlags(uint8_t flags)
{
    _flags = flags;
}

void FNPretty::setRenderLinks(bool on)
{
    if (on)
        _flags |= PRETTY_RENDER_LINKS;
    else
        _flags &= ~PRETTY_RENDER_LINKS;
}

void FNPretty::setQuery(const std::string &selector)
{
    _query = selector;
}

/**
 * "!key=value" option assignment, as an alternative to a query selector for
 * buses with no spare command byte to carry width/links/eol. See the header
 * for the recognized keys.
 */
PrettyOption FNPretty::setOption(const std::string &s)
{
    if (s.empty() || s[0] != '!')
        return PrettyOption::NotAnOption;

    size_t eq = s.find('=', 1);
    if (eq == std::string::npos)
        return PrettyOption::Invalid;

    std::string key = trimAscii(s.substr(1, eq - 1));
    std::string value = trimAscii(s.substr(eq + 1));
    if (key.empty() || value.empty())
        return PrettyOption::Invalid;

    for (char &c : key)
        c = (char)std::tolower(static_cast<unsigned char>(c));

    for (char c : value)
    {
        if (c < '0' || c > '9')
            return PrettyOption::Invalid;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long parsed = strtoul(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str())
        return PrettyOption::Invalid;

    if (key == "links")
    {
        setRenderLinks(parsed != 0);
        return PrettyOption::Applied;
    }
    else if (key == "width")
    {
        setScreenWidth((uint8_t)(parsed > 255 ? 255 : parsed));
        return PrettyOption::Applied;
    }
    else if (key == "eol")
    {
        if (parsed == 0 || parsed > 255)
            return PrettyOption::Invalid;
        setLineEnding(std::string(1, (char)parsed));
        return PrettyOption::Applied;
    }

    return PrettyOption::Invalid;
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

    // The raw body is gone; the parsed tree lives on in _doc for rerender().
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
    return renderHtml(body);
}

/**
 * Parse the body with gumbo-query (which, on ESP, routes Gumbo's allocations
 * to PSRAM) and retain the tree, then lay it out.
 */
bool FNPretty::renderHtml(const std::string &body)
{
    if (_doc != nullptr)
    {
        delete _doc;
        _doc = nullptr;
    }

    CDocument *doc = new CDocument();
    doc->parse(body);
    _doc = doc;

    return renderFromDocument();
}

/**
 * Re-render the retained document with whatever query/width/flags are
 * currently set, without touching the protocol or reparsing.
 */
bool FNPretty::rerender()
{
    if (_doc == nullptr || _doc->root() == nullptr)
        return false;

    renderFromDocument();
    return !_rendered.empty();
}

/**
 * The shared render path: reset layout state, collect links (if enabled),
 * then lay out either the whole document or each accepted match of _query.
 */
bool FNPretty::renderFromDocument()
{
    _rendered.clear();
    _pending.clear();
    _readPos = 0;
    _indent = 0;
    _pendingBlank = 0;
    _capture = 0;
    _listStack.clear();

    if (_doc == nullptr || _doc->root() == nullptr)
        return false;

    if (renderLinks())
        collectLinks();
    else
    {
        _links.clear();
        _linkNumbers.clear();
        _linkIndex.clear();
    }

    if (_query.empty())
    {
        renderHtmlNode(_doc->root());
        flushBlock();
    }
    else
    {
        // gumbo-query is patched exception-free: a malformed selector yields
        // an empty selection, so no try/catch is needed (the ESP firmware
        // builds with C++ exceptions disabled).
        CSelection sel = _doc->find(_query);

        std::vector<const GumboNode *> accepted;
        for (size_t i = 0; i < sel.nodeNum(); i++)
        {
            const GumboNode *n = sel.nodeAt(i).node();
            if (n == nullptr)
                continue;

            // Skip a match that is a descendant of an already-accepted match,
            // so an outer and an inner div both matching doesn't duplicate.
            bool isDescendant = false;
            for (const GumboNode *anc = n->parent; anc != nullptr && !isDescendant; anc = anc->parent)
            {
                for (const GumboNode *a : accepted)
                {
                    if (a == anc)
                    {
                        isDescendant = true;
                        break;
                    }
                }
            }
            if (!isDescendant)
                accepted.push_back(n);
        }

        for (size_t i = 0; i < accepted.size(); i++)
        {
            if (i > 0)
                requestBlank(1);
            renderHtmlNode(accepted[i]);
            flushBlock();
        }
    }

    // Trailing blank lines look wrong on a client that just reads to EOF.
    while (!lineEnding.empty() && _rendered.size() >= lineEnding.size() &&
           _rendered.compare(_rendered.size() - lineEnding.size(), lineEnding.size(), lineEnding) == 0)
    {
        _rendered.resize(_rendered.size() - lineEnding.size());
    }

    Debug_printf("FNPretty::renderFromDocument() - rendered %zu bytes at %u columns\r\n",
                 _rendered.size(), _screenWidth);
    return true;
}

// ---------------------------------------------------------------------------
// Link collection
// ---------------------------------------------------------------------------

void FNPretty::collectLinks()
{
    _links.clear();
    _linkNumbers.clear();
    _linkIndex.clear();

    GumboNode *root = _doc->root();
    if (root == nullptr)
        return;

    // An in-document <base href> overrides _baseUrl for every link; find it
    // first so anchor order doesn't matter.
    std::string effectiveBase = _baseUrl;
    bool found = false;
    findBaseHref(root, effectiveBase, found);

    collectLinksWalk(root, effectiveBase);
}

void FNPretty::findBaseHref(const GumboNode *node, std::string &effectiveBase, bool &found)
{
    if (found || node == nullptr)
        return;
    if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
        return;

    if (node->v.element.tag == GUMBO_TAG_BASE)
    {
        std::string href = getAttribute(node, "href");
        if (!href.empty())
        {
            effectiveBase = resolveUrl(_baseUrl, href);
            found = true;
            return;
        }
    }

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length && !found; i++)
        findBaseHref(static_cast<const GumboNode *>(children->data[i]), effectiveBase, found);
}

void FNPretty::collectLinksWalk(const GumboNode *node, const std::string &effectiveBase)
{
    if (node == nullptr)
        return;
    if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
        return;

    if (node->v.element.tag == GUMBO_TAG_A)
    {
        std::string href = trimAscii(getAttribute(node, "href"));
        if (!href.empty() && href[0] != '#')
        {
            std::string resolved = resolveUrl(effectiveBase, href);

            size_t num;
            auto it = _linkNumbers.find(resolved);
            if (it != _linkNumbers.end())
            {
                num = it->second;
            }
            else
            {
                _links.push_back(resolved);
                num = _links.size();
                _linkNumbers[resolved] = num;
            }
            _linkIndex[node] = num;
        }
    }

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        collectLinksWalk(static_cast<const GumboNode *>(children->data[i]), effectiveBase);
}

std::string FNPretty::linkUrl(size_t index) const
{
    if (index == 0 || index > _links.size())
        return "";
    return _links[index - 1];
}

// ---------------------------------------------------------------------------
// Layout walk
// ---------------------------------------------------------------------------

/**
 * Walk the tree, accumulating inline text and dispatching block elements to
 * their per-tag renderer. While _capture > 0 (collecting the text of a
 * subtree for a heading, table cell or link), every element is treated as
 * plain inline: recurse into children, no margins, no bullets/boxes/grids.
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

    if (tag == GUMBO_TAG_IMG)
    {
        // <img> is a void element - it has no children/text of its own, so
        // without this it renders nothing at all. Give it a visible inline
        // placeholder instead, built from its alt attribute (Gumbo has
        // already entity-decoded it, so - as in appendInline() - it must be
        // folded with fn_utf8_to_ascii() and not run through
        // fn_decode_entities() again) with internal whitespace collapsed and
        // the ends trimmed, the same way ordinary text is normalized. An alt
        // that is missing or all whitespace collapses to empty, and renders
        // as "[IMG]" rather than "[IMG ]". Appended straight to _pending
        // (like the link marker above) rather than via appendInline(), so
        // whitespace collapsing never disturbs the brackets, and handled
        // ahead of the _capture > 0 branch so an image inside captured text
        // (a heading, a table cell, link text) still comes through.
        std::string alt = fn_utf8_to_ascii(getAttribute(node, "alt"));
        std::string collapsed;
        for (char c : alt)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
            {
                if (!collapsed.empty() && collapsed.back() != ' ')
                    collapsed.push_back(' ');
            }
            else
            {
                collapsed.push_back(c);
            }
        }
        collapsed = trimAscii(collapsed);
        _pending += collapsed.empty() ? "[IMG]" : "[IMG " + collapsed + "]";
        return;
    }

    if (tag == GUMBO_TAG_A && renderLinks())
    {
        auto it = _linkIndex.find(node);
        if (it != _linkIndex.end())
        {
            std::string text = captureInline(node);
            if (!text.empty())
                _pending += "<" + text + ">[" + std::to_string(it->second) + "]";
            return;
        }
        // No entry (e.g. a fragment-only href) - fall through, treat as
        // ordinary inline content below.
    }

    if (_capture > 0)
    {
        // Block boundaries still separate words while capturing: flushBlock()
        // appends a separator space instead of emitting (a heading or a nested
        // table inside a <td>, say, must not run into the surrounding text).
        bool block = isBlockTag(tag);
        if (block)
            flushBlock();

        const GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; i++)
            renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));

        if (block)
            flushBlock();
        return;
    }

    switch (tag)
    {
    case GUMBO_TAG_P:
        renderMarginBlock(node, 1, 1);
        return;
    case GUMBO_TAG_HR:
        renderHr();
        return;
    case GUMBO_TAG_BLOCKQUOTE:
        renderBlockquote(node);
        return;
    case GUMBO_TAG_PRE:
        renderPre(node);
        return;
    case GUMBO_TAG_DL:
        renderMarginBlock(node, 1, 1);
        return;
    case GUMBO_TAG_DT:
        renderMarginBlock(node, 0, 0);
        return;
    case GUMBO_TAG_DD:
        renderDd(node);
        return;
    case GUMBO_TAG_H1:
        renderHeading(node, 1);
        return;
    case GUMBO_TAG_H2:
        renderHeading(node, 2);
        return;
    case GUMBO_TAG_H3:
        renderHeading(node, 3);
        return;
    case GUMBO_TAG_H4:
        renderHeading(node, 4);
        return;
    case GUMBO_TAG_H5:
        renderHeading(node, 5);
        return;
    case GUMBO_TAG_H6:
        renderHeading(node, 6);
        return;
    case GUMBO_TAG_UL:
        renderList(node, false);
        return;
    case GUMBO_TAG_OL:
        renderList(node, true);
        return;
    case GUMBO_TAG_LI:
        renderListItem(node);
        return;
    case GUMBO_TAG_TABLE:
        renderTable(node);
        return;
    default:
        break;
    }

    if (isBlockTag(tag))
    {
        renderMarginBlock(node, 0, 0);
        return;
    }

    // Ordinary inline element: just recurse, text runs together with siblings.
    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));
}

/**
 * Add text to the current block, collapsing runs of whitespace the way a
 * browser would.
 */
void FNPretty::appendInline(const char *text)
{
    if (text == nullptr)
        return;

    // Fold to printable ASCII before collapsing whitespace, so an 8-bit host
    // gets displayable output and column arithmetic elsewhere (wrapping,
    // table grids) counts the same bytes it will occupy on screen. This must
    // be fn_utf8_to_ascii(), never fn_decode_entities()/fn_sanitize_ascii():
    // Gumbo already decodes HTML character references before handing us
    // GumboText.text, so a second entity-decode pass here would double-decode
    // - text whose source said "&amp;lt;" (meant to display as the literal
    // "&lt;") would wrongly turn into "<". A multi-byte UTF-8 sequence never
    // spans two text nodes, so folding per-chunk here is safe.
    std::string folded = fn_utf8_to_ascii(text);

    for (const char *p = folded.c_str(); *p != '\0'; p++)
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
 * Emit the current block's text as wrapped line(s) at the current indent.
 * Blocks that hold no text produce no output at all, so the markup's nesting
 * doesn't turn into a stack of blank lines.
 *
 * While capturing (_capture > 0) a block boundary is not a line break at all
 * - it's just a word separator inside the string being captured.
 */
void FNPretty::flushBlock()
{
    if (_capture > 0)
    {
        if (!_pending.empty() && _pending.back() != ' ')
            _pending.push_back(' ');
        return;
    }

    size_t start = _pending.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        _pending.clear();
        return;
    }

    size_t end = _pending.find_last_not_of(' ');
    emitWrapped(_pending.substr(start, end - start + 1), spaces(_indent), _indent);
    _pending.clear();
}

/**
 * Render node's subtree as inline text only, returning it as a trimmed
 * string instead of emitting it. Used for heading text, table cells and link
 * text, where block children (e.g. <p> inside a <td>) must not start a new
 * line - they just get a separating space, via flushBlock()'s capture path.
 */
std::string FNPretty::captureInline(const GumboNode *node)
{
    std::string saved = _pending;
    _pending.clear();
    _capture++;

    if (node != nullptr && (node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE))
    {
        const GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; i++)
            renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));
    }

    _capture--;

    std::string result;
    size_t start = _pending.find_first_not_of(' ');
    if (start != std::string::npos)
    {
        size_t end = _pending.find_last_not_of(' ');
        result = _pending.substr(start, end - start + 1);
    }

    _pending = saved;
    return result;
}

// ---------------------------------------------------------------------------
// Emit primitives
// ---------------------------------------------------------------------------

void FNPretty::requestBlank(int n)
{
    if (n > _pendingBlank)
        _pendingBlank = n;
}

/**
 * Write out any queued blank lines now - unless nothing has been emitted
 * yet, in which case leading blanks are dropped so the document never starts
 * with blank lines.
 */
void FNPretty::flushPendingBlanks()
{
    if (!_rendered.empty())
    {
        for (int i = 0; i < _pendingBlank; i++)
            _rendered += lineEnding;
    }
    _pendingBlank = 0;
}

/**
 * Emit one already-formatted line, first flushing any queued blank lines.
 */
void FNPretty::emitRaw(const std::string &line)
{
    flushPendingBlanks();
    _rendered += line;
    _rendered += lineEnding;
}

/**
 * Word-wrap text to (_screenWidth - contIndent) columns (floor of 1), and
 * emit the first line with firstPrefix and the rest indented to contIndent.
 * Callers always pass a firstPrefix whose length equals contIndent, so the
 * wrap width is uniform across the whole block.
 */
void FNPretty::emitWrapped(const std::string &text, const std::string &firstPrefix, size_t contIndent)
{
    size_t width = _screenWidth > contIndent ? (size_t)_screenWidth - contIndent : 1;
    std::vector<std::string> lines = wrapText(text, width);

    for (size_t i = 0; i < lines.size(); i++)
        emitRaw((i == 0 ? firstPrefix : spaces(contIndent)) + lines[i]);
}

void FNPretty::emitCentered(const std::string &line, size_t width)
{
    if (line.size() >= width)
    {
        emitRaw(line);
        return;
    }
    emitRaw(spaces((width - line.size()) / 2) + line);
}

/**
 * Cap an indent so at least kMinTextWidth columns of text still fit, reserving
 * extra columns to the right of the indent (a list marker).
 */
size_t FNPretty::clampIndent(size_t want, size_t extra) const
{
    size_t budget = (size_t)_screenWidth > kMinTextWidth + extra
                        ? (size_t)_screenWidth - kMinTextWidth - extra
                        : 0;
    return want < budget ? want : budget;
}

// ---------------------------------------------------------------------------
// Per-element rendering
// ---------------------------------------------------------------------------

/**
 * The common shape for a block that just wants a blank-line margin before and
 * after its contents (div/section/p/dl/dt/... - the "generic container" and
 * <p> cases).
 */
void FNPretty::renderMarginBlock(const GumboNode *node, int before, int after)
{
    flushBlock();
    requestBlank(before);

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));

    flushBlock();
    requestBlank(after);
}

void FNPretty::renderDd(const GumboNode *node)
{
    flushBlock();

    size_t saved = _indent;
    _indent = clampIndent(_indent + 2);

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));

    flushBlock();
    _indent = saved;
}

void FNPretty::renderBlockquote(const GumboNode *node)
{
    flushBlock();
    requestBlank(1);

    size_t saved = _indent;
    _indent = clampIndent(_indent + 2);

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));

    flushBlock();
    _indent = saved;
    requestBlank(1);
}

void FNPretty::renderHr()
{
    flushBlock();
    requestBlank(1);

    size_t width = _screenWidth > _indent ? (size_t)_screenWidth - _indent : 1;
    emitRaw(spaces(_indent) + std::string(width, '-'));

    requestBlank(1);
}

/**
 * Collect the raw text of a <pre> subtree without collapsing whitespace, as
 * appendInline() does for everything else. A <br> inside is treated as a
 * literal newline, matching how a browser would render it.
 */
void FNPretty::collectRawText(const GumboNode *node, std::string &out)
{
    if (node == nullptr)
        return;

    switch (node->type)
    {
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_WHITESPACE:
    case GUMBO_NODE_CDATA:
        if (node->v.text.text != nullptr)
            out += node->v.text.text;
        return;
    case GUMBO_NODE_ELEMENT:
    case GUMBO_NODE_TEMPLATE:
        break;
    default:
        return;
    }

    if (isSkippedTag(node->v.element.tag))
        return;
    if (node->v.element.tag == GUMBO_TAG_BR)
    {
        out += '\n';
        return;
    }

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        collectRawText(static_cast<const GumboNode *>(children->data[i]), out);
}

void FNPretty::renderPre(const GumboNode *node)
{
    flushBlock();
    requestBlank(1);

    std::string raw;
    collectRawText(node, raw);

    // HTML parsers drop a single leading newline right after <pre>.
    if (!raw.empty() && raw.front() == '\n')
        raw.erase(0, 1);

    std::vector<std::string> lines = splitLines(raw);
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();

    size_t width = _screenWidth > _indent ? (size_t)_screenWidth - _indent : 1;
    for (const std::string &rawLine : lines)
    {
        // Fold to ASCII per line, after splitting on '\n' and expanding tabs
        // (not on the raw block as a whole): fn_utf8_to_ascii() turns '\n'
        // into a space, which would destroy the very line structure <pre> is
        // meant to preserve. Tabs are already spaces by this point, so
        // folding after expandTabs() loses nothing.
        std::string line = fn_utf8_to_ascii(expandTabs(rawLine));
        if (line.empty())
        {
            emitRaw(spaces(_indent));
            continue;
        }
        size_t pos = 0;
        while (pos < line.size())
        {
            size_t take = std::min(width, line.size() - pos);
            emitRaw(spaces(_indent) + line.substr(pos, take));
            pos += take;
        }
    }

    requestBlank(1);
}

void FNPretty::renderHeading(const GumboNode *node, int level)
{
    flushBlock();

    std::string text = captureInline(node);
    if (text.empty())
        return;

    switch (level)
    {
    case 1:
        renderH1(text);
        break;
    case 2:
        renderH2(text);
        break;
    case 3:
    case 4:
        renderHeadingUnderline(text, '=');
        break;
    default: // 5, 6
        renderHeadingUnderline(text, '-');
        break;
    }
}

/**
 * h1/h2 centering ignores _indent - they're always relative to the full
 * screen width.
 */
void FNPretty::renderH1(const std::string &text)
{
    requestBlank(2);

    size_t wrapWidth = _screenWidth > 4 ? (size_t)_screenWidth - 4 : 1;
    std::vector<std::string> lines = wrapText(text, wrapWidth);

    size_t maxLen = 0;
    for (const std::string &l : lines)
        maxLen = std::max(maxLen, l.size());

    size_t boxInner = std::min((size_t)(_screenWidth > 2 ? _screenWidth - 2 : 1), maxLen + 2);
    size_t boxWidth = boxInner + 2;
    size_t leftPad = _screenWidth > boxWidth ? ((size_t)_screenWidth - boxWidth) / 2 : 0;

    emitRaw(spaces(leftPad) + "+" + std::string(boxInner, '-') + "+");
    for (const std::string &l : lines)
    {
        size_t pad = boxInner > l.size() ? boxInner - l.size() : 0;
        size_t left = pad / 2;
        size_t right = pad - left;
        emitRaw(spaces(leftPad) + "|" + spaces(left) + l + spaces(right) + "|");
    }
    emitRaw(spaces(leftPad) + "+" + std::string(boxInner, '-') + "+");

    requestBlank(2);
}

void FNPretty::renderH2(const std::string &text)
{
    requestBlank(2);

    std::vector<std::string> lines = wrapText(text, _screenWidth);
    for (const std::string &l : lines)
        emitCentered(l, _screenWidth);

    requestBlank(2);
}

void FNPretty::renderHeadingUnderline(const std::string &text, char ch)
{
    requestBlank(1);

    size_t width = _screenWidth > _indent ? (size_t)_screenWidth - _indent : 1;
    std::vector<std::string> lines = wrapText(text, width);

    size_t maxLen = 0;
    for (const std::string &l : lines)
    {
        emitRaw(spaces(_indent) + l);
        maxLen = std::max(maxLen, l.size());
    }

    emitRaw(spaces(_indent) + std::string(std::min(maxLen, width), ch));

    requestBlank(1);
}

void FNPretty::renderList(const GumboNode *node, bool ordered)
{
    bool nested = node->parent != nullptr && node->parent->type == GUMBO_NODE_ELEMENT &&
                  node->parent->v.element.tag == GUMBO_TAG_LI;

    flushBlock();
    if (!nested)
        requestBlank(1);

    ListState state;
    state.ordered = ordered;
    state.number = 1;
    state.digits = 1;

    if (ordered)
    {
        int start = 1;
        std::string startAttr = getAttribute(node, "start");
        if (!startAttr.empty())
        {
            char *end = nullptr;
            long v = strtol(startAttr.c_str(), &end, 10);
            if (end != startAttr.c_str())
                start = (int)v;
        }

        int count = 0;
        const GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; i++)
        {
            const GumboNode *c = static_cast<const GumboNode *>(children->data[i]);
            if (c->type == GUMBO_NODE_ELEMENT && c->v.element.tag == GUMBO_TAG_LI)
                count++;
        }

        long last = (long)start + (count > 0 ? count - 1 : 0);
        long av = last < 0 ? -last : last;
        int digits = 1;
        while (av >= 10)
        {
            av /= 10;
            digits++;
        }

        state.number = start;
        state.digits = digits;
    }

    _listStack.push_back(state);

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));

    _listStack.pop_back();

    flushBlock();
    if (!nested)
        requestBlank(1);
}

/**
 * A list item's marker has to attach to its *first* emitted line, which may
 * come from a nested block (e.g. <li><p>...</p></li>). Simplest correct
 * approach: indent the item's contents under the marker as normal, remember
 * where the item's output starts, and afterwards overwrite the leading
 * spaces of that first line with the marker. If the item produced no output,
 * nothing is emitted at all.
 */
void FNPretty::renderListItem(const GumboNode *node)
{
    flushBlock();

    if (_listStack.empty())
    {
        // Malformed markup: an <li> with no enclosing list. Degrade to a
        // plain block rather than crashing on an empty stack.
        renderMarginBlock(node, 0, 0);
        return;
    }

    ListState &state = _listStack.back();

    std::string marker;
    size_t markerWidth;
    if (state.ordered)
    {
        std::string numStr = std::to_string(state.number);
        size_t pad = numStr.size() < (size_t)state.digits ? (size_t)state.digits - numStr.size() : 0;
        marker = spaces(pad) + numStr + ". ";
        markerWidth = (size_t)state.digits + 2;
        state.number++;
    }
    else
    {
        marker = "* ";
        markerWidth = 2;
    }

    size_t markerCol = clampIndent(_indent + kListIndent, markerWidth);
    std::string firstPrefix = spaces(markerCol) + marker;
    size_t contIndent = markerCol + markerWidth;

    size_t saved = _indent;
    _indent = contIndent;

    // Force out any margin blank lines still queued from before this item
    // (e.g. the list's own before-margin) so startPos below lands exactly on
    // the first byte of this item's own first content line, not a blank line.
    flushPendingBlanks();
    size_t startPos = _rendered.size();

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
        renderHtmlNode(static_cast<const GumboNode *>(children->data[i]));
    flushBlock();

    _indent = saved;

    if (_rendered.size() > startPos)
    {
        size_t n = firstPrefix.size();
        if (startPos + n <= _rendered.size())
        {
            for (size_t i = 0; i < n; i++)
                _rendered[startPos + i] = firstPrefix[i];
        }
    }
}

/**
 * Descend the table gathering every <tr> in document order (through
 * thead/tbody/tfoot), but do not descend into a nested <table> - it is left
 * unrendered rather than flattened into the outer grid.
 */
void FNPretty::collectTableRows(const GumboNode *node, std::vector<std::vector<std::string>> &rows)
{
    if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
        return;

    const GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; i++)
    {
        const GumboNode *child = static_cast<const GumboNode *>(children->data[i]);
        if (child->type != GUMBO_NODE_ELEMENT)
            continue;

        GumboTag tag = child->v.element.tag;
        if (tag == GUMBO_TAG_TABLE)
            continue; // nested tables are not rendered

        if (tag == GUMBO_TAG_TR)
        {
            std::vector<std::string> row;
            const GumboVector *cells = &child->v.element.children;
            for (unsigned int j = 0; j < cells->length; j++)
            {
                const GumboNode *cell = static_cast<const GumboNode *>(cells->data[j]);
                if (cell->type != GUMBO_NODE_ELEMENT)
                    continue;
                GumboTag ctag = cell->v.element.tag;
                // colspan/rowspan are ignored - every cell is treated as 1x1.
                if (ctag == GUMBO_TAG_TD || ctag == GUMBO_TAG_TH)
                    row.push_back(captureInline(cell));
            }
            rows.push_back(std::move(row));
        }
        else
        {
            collectTableRows(child, rows);
        }
    }
}

void FNPretty::renderTable(const GumboNode *node)
{
    if (isLayoutTable(node))
    {
        // Not a data table - render its contents transparently as ordinary
        // blocks (see isLayoutTable()'s comment). table/tbody/thead/tfoot/tr/
        // td/th are all in isBlockTag(), so the plain margin-0/0 generic path
        // below gives each cell its own block and re-runs this same
        // classification on any table nested inside it.
        renderMarginBlock(node, 0, 0);
        return;
    }

    flushBlock();
    requestBlank(1);

    std::vector<std::vector<std::string>> rows;
    collectTableRows(node, rows);

    size_t ncols = 0;
    for (const auto &row : rows)
        ncols = std::max(ncols, row.size());

    if (rows.empty() || ncols == 0)
    {
        requestBlank(1);
        return;
    }

    std::vector<size_t> natural(ncols, 0);
    for (const auto &row : rows)
        for (size_t c = 0; c < row.size(); c++)
            natural[c] = std::max(natural[c], row[c].size());

    // Below kOuterBorderMinWidth the table drops its left/right border,
    // trading 4 columns of frame for 4 columns of content.
    bool bordered = _screenWidth >= kOuterBorderMinWidth;
    size_t overhead = bordered ? (3 * ncols + 1) : (3 * ncols - 3);
    size_t avail = _screenWidth > _indent + overhead ? (size_t)_screenWidth - _indent - overhead : 0;

    if (avail < ncols)
    {
        // Too narrow to draw a grid: fall back to a wrapped paragraph per row.
        for (const auto &row : rows)
        {
            std::string line;
            for (size_t c = 0; c < row.size(); c++)
            {
                if (c > 0)
                    line += " | ";
                line += row[c];
            }
            emitWrapped(line, spaces(_indent), _indent);
        }
        requestBlank(1);
        return;
    }

    size_t sumNatural = 0;
    for (size_t n : natural)
        sumNatural += n;

    std::vector<size_t> w;
    if (sumNatural <= avail)
    {
        w = natural;
    }
    else
    {
        w.resize(ncols);
        for (size_t c = 0; c < ncols; c++)
            w[c] = std::max((size_t)1, natural[c] * avail / sumNatural);

        size_t sumW = 0;
        for (size_t x : w)
            sumW += x;

        while (sumW < avail)
        {
            size_t best = 0;
            long bestDeficit = -1;
            for (size_t c = 0; c < ncols; c++)
            {
                long deficit = (long)natural[c] - (long)w[c];
                if (deficit > bestDeficit)
                {
                    bestDeficit = deficit;
                    best = c;
                }
            }
            w[best]++;
            sumW++;
        }
        while (sumW > avail)
        {
            size_t best = (size_t)-1;
            size_t bestWidth = 0;
            for (size_t c = 0; c < ncols; c++)
            {
                if (w[c] > 1 && w[c] > bestWidth)
                {
                    bestWidth = w[c];
                    best = c;
                }
            }
            if (best == (size_t)-1)
                break; // can't shrink further
            w[best]--;
            sumW--;
        }
    }

    std::string separator;
    if (bordered)
    {
        separator = spaces(_indent) + "+";
        for (size_t c = 0; c < ncols; c++)
            separator += std::string(w[c] + 2, '-') + "+";
    }
    else
    {
        // No outer frame: columns are joined by "-+-", with no leading or
        // trailing border character.
        separator = spaces(_indent);
        for (size_t c = 0; c < ncols; c++)
        {
            if (c > 0)
                separator += "-+-";
            separator += std::string(w[c], '-');
        }
    }

    emitRaw(separator);
    for (const auto &row : rows)
    {
        std::vector<std::vector<std::string>> cellLines(ncols);
        size_t height = 1;
        for (size_t c = 0; c < ncols; c++)
        {
            std::string text = c < row.size() ? row[c] : "";
            cellLines[c] = wrapText(text, w[c]);
            if (cellLines[c].empty())
                cellLines[c].push_back("");
            height = std::max(height, cellLines[c].size());
        }

        for (size_t k = 0; k < height; k++)
        {
            std::string line = spaces(_indent);
            if (bordered)
                line += "|";
            for (size_t c = 0; c < ncols; c++)
            {
                std::string cell = k < cellLines[c].size() ? cellLines[c][k] : "";
                // The last column of a borderless row is left unpadded; any
                // whitespace it would have contributed is trimmed below.
                bool padCell = bordered || c + 1 < ncols;
                if (padCell && cell.size() < w[c])
                    cell += spaces(w[c] - cell.size());

                if (bordered)
                    line += " " + cell + " |";
                else
                {
                    if (c > 0)
                        line += " | ";
                    line += cell;
                }
            }

            if (!bordered)
            {
                // Strip trailing whitespace: besides the unpadded last
                // column, a wrapped continuation line can leave trailing
                // columns empty.
                size_t last = line.find_last_not_of(' ');
                line = last == std::string::npos ? std::string() : line.substr(0, last + 1);
            }

            emitRaw(line);
        }
        emitRaw(separator);
    }

    requestBlank(1);
}

// ---------------------------------------------------------------------------
// Read side (unchanged semantics)
// ---------------------------------------------------------------------------

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
