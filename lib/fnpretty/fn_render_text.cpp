#include "fn_render_text.h"

#include "fnpretty.h"
#include "../fntext/fn_sanitize.h"

std::string fn_render_html_text(const std::string &html, uint8_t width, const std::string &eol)
{
    FNPretty pretty;

    pretty.setLineEnding("\n");
    pretty.setScreenWidth(width != 0 ? width : FN_RENDER_TEXT_DEFAULT_WIDTH);
    // Defaults already give ASCII furniture and no link rendering.

    std::string text;
    if (pretty.renderDocument(html))
        text = pretty.rendered();
    else
        text = fn_sanitize_ascii(html);

    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();

    // Wrap and trim on "\n" above, then break lines with the caller's EOL so a
    // value reads as lines on the host (0x9B on the Atari, not a stray 0x0A).
    if (eol == "\n")
        return text;
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        if (c == '\n')
            out += eol;
        else
            out += c;
    }
    return out;
}
