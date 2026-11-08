#include "fn_render_text.h"

#include "fnpretty.h"
#include "../fntext/fn_sanitize.h"

std::string fn_render_html_text(const std::string &html, uint8_t width)
{
    FNPretty pretty;

    pretty.setLineEnding("\n");
    pretty.setScreenWidth(width != 0 ? width : FN_RENDER_TEXT_DEFAULT_WIDTH);
    // Defaults already give ASCII furniture and no link rendering.

    std::string text;
    if (pretty.renderDocument(html))
        text = pretty.rendered();
    else
        return fn_sanitize_ascii(html);

    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    return text;
}
