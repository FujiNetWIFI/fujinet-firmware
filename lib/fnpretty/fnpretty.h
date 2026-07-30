/**
 * Pretty renderer for #FujiNet
 *
 * Fetches a document from the bound protocol and renders it as lightly
 * formatted plain text, word-wrapped to the client's screen width (32/40/80
 * columns). HTML is the only format handled so far - parsed with Gumbo (HTML5
 * tree construction, robust error recovery) - but the read side is
 * format-agnostic, so other text-based formats (markdown, gophermap, ...) slot
 * in behind renderDocument() as they are added.
 *
 * Unlike FNSGML - which resolves a CSS selector and hands back one match at a
 * time - the whole document is rendered up front and then read sequentially,
 * so the client just does PARSE followed by STATUS/READ until empty.
 */

#ifndef FNPRETTY_H
#define FNPRETTY_H

#include <stddef.h>
#include <stdint.h>
#include <string>

// Only the status object is needed by value here; the protocol itself is used
// solely through this pointer, so Protocol.h (and the bus headers behind it)
// stays out of everything that includes us.
#include "../network-protocol/networkStatus.h"

class NetworkProtocol;

struct GumboInternalNode;
typedef struct GumboInternalNode GumboNode;

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

private:
    NetworkProtocol *_protocol = nullptr;
    std::string lineEnding;
    uint8_t _screenWidth = 40;

    /**
     * Raw document body, only held for the duration of parse().
     */
    std::string _parseBuffer;

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
     * @brief Drain the whole body from the protocol into _parseBuffer.
     */
    bool fetch();

    /**
     * @brief Render an HTML document into _rendered.
     */
    bool renderHtml(const std::string &body);

    void renderHtmlNode(const GumboNode *node);
    void appendInline(const char *text);
    void flushBlock();
    void emitWrapped(const std::string &text);
    void emitLine(const std::string &line);
};

#endif /* FNPRETTY_H */
