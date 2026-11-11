/**
 * Pretty renderer for #FujiNet
 *
 * Fetches a document from the bound protocol and renders it as lightly
 * formatted plain text, word-wrapped to the client's screen width (32/40/80
 * columns). HTML is the only format handled so far - parsed with Gumbo (HTML5
 * tree construction, robust error recovery) via gumbo-query's CDocument - but
 * the read side is format-agnostic, so other text-based formats (markdown,
 * gophermap, ...) slot in behind renderDocument() as they are added.
 *
 * Unlike FNSGML - which resolves a CSS selector and hands back one match at a
 * time - the whole document (or the queried section of it) is rendered up
 * front and then read sequentially, so the client just does PARSE followed by
 * STATUS/READ until empty.
 *
 * The parsed document is retained (see CDocument *_doc) so a client can change
 * the query and/or screen width and call rerender() to re-lay-out the same
 * page without re-fetching it.
 */

#ifndef FNPRETTY_H
#define FNPRETTY_H

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

// Only the status object is needed by value here; the protocol itself is used
// solely through this pointer, so Protocol.h (and the bus headers behind it)
// stays out of everything that includes us.
#include "../network-protocol/networkStatus.h"

class NetworkProtocol;
class CDocument; // gumbo-query

struct GumboInternalNode;
typedef struct GumboInternalNode GumboNode;

enum PrettyFlags_t {
    PRETTY_RENDER_LINKS = 0x01,
};

/**
 * Character set the renderer draws its *furniture* with - box frames around
 * headings and tables, horizontal rules, list bullets and heading underlines.
 * Page text itself is always sanitized to printable ASCII; this only selects
 * the glyphs the layout draws around it.
 *
 * Ascii is the portable default. The rest are host-native sets, which a
 * client selects generically ("give me native graphics") rather than by name:
 * the bus device declares its platform with setNativeCharset() and the client
 * asks for it with setNativeGraphics(true).
 */
enum class PrettyCharset : uint8_t {
    Ascii = 0,   // 7-bit ASCII: +--+ frames, '*' bullets, '=' / '-' underlines
    Atascii = 1, // Atari: ATASCII box graphics and inverse video
    MouseText = 2, // Apple II: MouseText line glyphs and inverse video. The
                   // wire byte n (0x00-0x1F) stands for MouseText glyph
                   // $40+n; the client maps it, since $40-$5F are also
                   // ASCII. Bit 7 set means inverse, as on the Atari.
};

/**
 * The glyphs and capabilities of one PrettyCharset. Single bytes throughout,
 * so a glyph is always exactly one screen column and the layout can keep
 * counting columns with std::string::size().
 */
struct PrettyGlyphs
{
    char horiz;         // horizontal frame run, and <hr>
    char vert;          // vertical frame
    char cornerTL;      // frame corners, clockwise from top left
    char cornerTR;
    char cornerBR;
    char cornerBL;
    char teeTop;        // T junctions: on the top edge, bottom edge, ...
    char teeBottom;
    char teeLeft;       // ... left edge, right edge, and the interior cross
    char teeRight;
    char cross;
    char bullet;        // <ul> marker
    char underline;     // h4 underline
    char underlineThin; // h5/h6 underline

    bool boxed;         // has true box-drawing glyphs (not just +-|)
    bool inverse;       // has inverse video (bit 7 set)
    bool footRule;      // horiz sits at the foot of its cell, so a frame's
                        // top edge needs a blank framed row under it
};

class FNPretty
{
public:
    FNPretty();
    virtual ~FNPretty();

    void setLineEnding(const std::string &_lineEnding);
    void setProtocol(NetworkProtocol *newProtocol);

    /**
     * @brief Set the client's screen width in columns, e.g. 32, 40 or 80.
     * 0 leaves the current width alone; anything smaller than the minimum
     * usable width is clamped.
     */
    void setScreenWidth(uint8_t width);
    uint8_t screenWidth() const { return _screenWidth; }

    /**
     * @brief Set the page URL, used to resolve relative hrefs into absolute
     * links. Stores only; does not re-render.
     */
    void setBaseUrl(const std::string &url);

    void setFlags(uint8_t flags);
    uint8_t flags() const { return _flags; }

    /**
     * @brief Convenience over PRETTY_RENDER_LINKS.
     */
    void setRenderLinks(bool on);
    bool renderLinks() const { return (_flags & PRETTY_RENDER_LINKS) != 0; }

    /**
     * @brief Declare the host platform's native character set - what
     * setNativeGraphics(true) resolves to on this bus. Stores
     * only; it does not change the set currently in use.
     */
    void setNativeCharset(PrettyCharset cs);
    PrettyCharset nativeCharset() const { return _nativeCharset; }

    /**
     * @brief Select the character set the layout draws with. Stores only -
     * the caller re-renders.
     */
    void setCharset(PrettyCharset cs);
    PrettyCharset charset() const { return _charset; }

    /**
     * @brief Convenience over setCharset(): true selects nativeCharset(),
     * false plain ASCII. This is what clients use, so a program does not have
     * to know which platform it is talking to.
     */
    void setNativeGraphics(bool on);
    bool nativeGraphics() const { return _charset != PrettyCharset::Ascii; }

    /**
     * @brief Set a CSS selector naming the section of the document to render;
     * an empty selector (the default) renders the whole document. Stores
     * only - call rerender() (or parse()/renderDocument() again) to apply it.
     */
    void setQuery(const std::string &selector);
    const std::string &query() const { return _query; }

    /**
     * @brief Fetch the document from the protocol and render it to text.
     * @return true if a document was fetched and rendered.
     */
    bool parse();

    /**
     * @brief Render an already-fetched document, replacing any previous one.
     * parse() is this plus the fetch; also the entry point tests use.
     *
     * Everything fetched is treated as HTML for now; this is where a format
     * choice (sniffed, or set by the client) will dispatch.
     *
     * @return true if the document parsed.
     */
    bool renderDocument(const std::string &body);

    /**
     * @brief Re-render the retained document with the current query, screen
     * width and flags, without re-fetching or re-parsing it. Resets the read
     * cursor.
     * @return false if there is no retained document, or nothing rendered
     * (e.g. the query matched nothing).
     */
    bool rerender();

    /**
     * @brief Bytes of rendered text not yet handed to the client.
     */
    size_t available() const { return _rendered.size() - _readPos; }

    /**
     * @brief Copy up to len bytes of rendered text out, advancing the cursor.
     * @return number of bytes actually copied (0 at end of document).
     */
    size_t readValue(uint8_t *buf, size_t len);

    bool status(NetworkStatus *status);

    /**
     * @brief The full rendered document (mainly for tests/debugging).
     */
    const std::string &rendered() const { return _rendered; }

    /**
     * @brief Number of distinct links collected (only populated when
     * renderLinks() is on).
     */
    size_t linkCount() const { return _links.size(); }

    /**
     * @brief Resolved URL for link number index (1-based, matching the [n]
     * markers in the rendered text). Returns "" if index is out of range.
     */
    std::string linkUrl(size_t index) const;

    /**
     * @brief Value of attribute attr on the first element matching the CSS
     * selector, from the retained document (no re-fetch). Returned as in the
     * document - no URL resolution - but folded to ASCII like page text.
     * Returns "" if there is no document, no match, or the attribute is
     * missing or empty.
     */
    std::string attrOfFirst(const std::string &selector, const std::string &attr) const;

private:
    struct ListState
    {
        bool ordered = false;
        int number = 1;
        int digits = 1;
    };

    NetworkProtocol *_protocol = nullptr;
    std::string lineEnding;
    uint8_t _screenWidth = 40;
    std::string _baseUrl;
    uint8_t _flags = 0;
    std::string _query;

    /**
     * Character set in use, and what "native" means on this bus. Default
     * ASCII on both, so a device that never calls setNativeCharset() renders
     * exactly as it always has.
     */
    PrettyCharset _charset = PrettyCharset::Ascii;
    PrettyCharset _nativeCharset = PrettyCharset::Ascii;

    /**
     * Raw document body, only held for the duration of parse().
     */
    std::string _parseBuffer;

    /**
     * Parsed document tree, retained across renders so rerender() (query,
     * width or flag changes) doesn't need to refetch and reparse the page.
     */
    CDocument *_doc = nullptr;

    /**
     * Rendered text and how much of it the client has consumed.
     */
    std::string _rendered;
    size_t _readPos = 0;

    /**
     * Inline text collected for the block currently being rendered. Flushed
     * (wrapped and emitted) whenever a block boundary or <br> is reached.
     */
    std::string _pending;

    /**
     * Column at which normal block text starts; grows for blockquote/dd/li
     * nesting and is restored on the way back out.
     */
    size_t _indent = 0;

    /**
     * Blank lines queued before the next emitted line. Requests are
     * max-merged, not summed, and dropped entirely at the very start of the
     * output.
     */
    int _pendingBlank = 0;

    /**
     * >0 while capturing a subtree's text as a single string (link text,
     * table cells, headings): flushBlock() appends a separator space to
     * _pending instead of emitting, and block/table/list/heading elements are
     * treated as plain inline.
     */
    int _capture = 0;

    /**
     * List nesting state (ordered?, next number, digit width for alignment).
     */
    std::vector<ListState> _listStack;

    /**
     * Collected links (only when renderLinks() is set): index i holds link
     * number i+1's resolved URL; _linkNumbers maps a resolved URL back to its
     * number (for de-duplication); _linkIndex maps an anchor element to its
     * number so the layout pass doesn't have to re-resolve it.
     */
    std::vector<std::string> _links;
    std::map<std::string, size_t> _linkNumbers;
    std::map<const GumboNode *, size_t> _linkIndex;

    /**
     * @brief Drain the whole body from the protocol into _parseBuffer.
     */
    bool fetch();

    /**
     * @brief Parse an HTML document with gumbo-query, retaining it in _doc,
     * then render it.
     */
    bool renderHtml(const std::string &body);

    /**
     * @brief Render the currently retained document (query/width/flags as
     * currently set) into _rendered. Shared by renderHtml() and rerender().
     */
    bool renderFromDocument();

    // -- link collection (phase 1 of a render) --
    void collectLinks();
    void findBaseHref(const GumboNode *node, std::string &effectiveBase, bool &found);
    void collectLinksWalk(const GumboNode *node, const std::string &effectiveBase);

    // -- layout walk --
    void renderHtmlNode(const GumboNode *node);
    void appendInline(const char *text);
    void flushBlock();
    std::string captureInline(const GumboNode *node);

    /**
     * @brief Cap an indent so at least kMinTextWidth columns of text still fit,
     * reserving extra columns to the right of the indent (a list marker).
     */
    size_t clampIndent(size_t want, size_t extra = 0) const;

    /**
     * @brief Glyph table for the character set currently selected.
     */
    const PrettyGlyphs &glyphs() const;

    /**
     * @brief Copy of s in inverse video (bit 7 set on every byte). Only
     * meaningful when glyphs().inverse is set.
     */
    std::string inverseText(const std::string &s) const;

    // -- emit primitives --
    void requestBlank(int n);
    void flushPendingBlanks();
    void emitRaw(const std::string &line);
    void emitWrapped(const std::string &text, const std::string &firstPrefix, size_t contIndent);
    void emitCentered(const std::string &line, size_t width);

    // -- per-element rendering --
    void renderMarginBlock(const GumboNode *node, int before, int after);
    void renderDd(const GumboNode *node);
    void renderBlockquote(const GumboNode *node);
    void renderHr();
    void renderPre(const GumboNode *node);
    void collectRawText(const GumboNode *node, std::string &out);
    void renderHeading(const GumboNode *node, int level);
    void renderH1(const std::string &text);
    void renderH2(const std::string &text);
    void renderBoxedHeading(const std::string &text, bool solid);
    void renderHeadingInverse(const std::string &text);
    void renderHeadingUnderline(const std::string &text, char ch);
    void renderList(const GumboNode *node, bool ordered);
    void renderListItem(const GumboNode *node);
    void renderTable(const GumboNode *node);
    std::string tableRule(const std::vector<size_t> &w, bool padded,
                          char left, char mid, char right) const;
    void collectTableRows(const GumboNode *node, std::vector<std::vector<std::string>> &rows);
};

#endif /* FNPRETTY_H */
