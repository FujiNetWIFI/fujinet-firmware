/**
 * XPath-subset query engine for FNXML.
 *
 * Separated from FNXML itself so the query semantics can be exercised without a
 * NetworkProtocol behind them.
 *
 * Supported subset:
 * - Steps separated by '/'. A leading '/' is optional; both '/rss/channel/item'
 *   and 'rss/channel/item' start at the root element.
 * - '//name' = descendant-or-self: match 'name' at any depth. Valid at the start
 *   ('//item') or mid-path ('/rss//title').
 * - '*' as a step matches any element name.
 * - Element names are case-sensitive and namespace prefixes are part of the name
 *   ('dc:creator'). A bare local name also matches a prefixed element, so
 *   'creator' finds 'dc:creator' too.
 * - Predicates, zero or more per step: '[n]' (1-based position), '[@attr]'
 *   (attribute present), '[@attr="value"]' (attribute equals), '[text()="value"]'
 *   (element text equals).
 * - A trailing attribute selector returns the attribute value instead of the
 *   element text, in either '/path/@attr' or '/path@attr' form.
 * - An empty or malformed query matches nothing; it never throws or crashes,
 *   since the query arrives verbatim from an 8-bit host.
 */

#ifndef FNXML_QUERY_H
#define FNXML_QUERY_H

#include <stddef.h>
#include <string>

namespace tinyxml2 { class XMLDocument; }

/**
 * Resolve query against doc and write the matchIndex'th match into out.
 *
 * The value of a match is the concatenated text of all its descendants, or the
 * named attribute's value if the query ends in an attribute selector.
 *
 * @return true if that match existed (out holds its value), false otherwise
 *         (out is left empty).
 */
bool fnxml_resolve_query(tinyxml2::XMLDocument *doc, const std::string &query,
                         size_t matchIndex, std::string &out);

#endif /* FNXML_QUERY_H */
