/**
 * HTML-to-plain-text helper for the query parsers' TEXT output mode.
 *
 * Lives beside FNPretty (and not in lib/fntext, which stays free of parser
 * dependencies) because it is FNPretty's renderer with the settings a query
 * value wants.
 */

#ifndef FN_RENDER_TEXT_H
#define FN_RENDER_TEXT_H

#include <stdint.h>
#include <string>

/** Columns used when a caller passes a width of 0. */
#define FN_RENDER_TEXT_DEFAULT_WIDTH 40

/**
 * Render an HTML fragment as word-wrapped plain ASCII text.
 *
 * Tags are interpreted (paragraphs, lists, headings, tables, ...) rather than
 * stripped, entities are decoded and non-ASCII is folded to ASCII. Layout uses
 * ASCII furniture and links are not rendered. Lines are separated by "\n"
 * (0x0A) and there is no trailing line break; the caller appends its own line
 * ending as for any other value. No line is longer than width columns.
 *
 * @param html  the markup, e.g. an RSS <description> or Atom <content> value
 * @param width columns to wrap at; 0 means FN_RENDER_TEXT_DEFAULT_WIDTH
 */
std::string fn_render_html_text(const std::string &html, uint8_t width);

#endif /* FN_RENDER_TEXT_H */
