/**
 * XPath-subset query engine for FNXML.
 *
 * Kept free of the NetworkProtocol plumbing so it can be unit tested directly
 * (see tests/FnxmlQueryTests.cpp).
 */

#include "fnxml_query.h"

#include <string.h>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "tinyxml2.h"

namespace {

// Recursion depth limit to prevent stack overflow on hostile input.
constexpr int kMaxRecursionDepth = 64;

} // namespace

/**
 * Helper: Extract element string value (concatenation of all descendant text nodes).
 * Recurses with depth limit to prevent stack overflow on malformed input.
 */
static std::string getElementTextValue(const tinyxml2::XMLElement *elem, int depth = 0)
{
    if (depth > kMaxRecursionDepth || elem == nullptr)
        return "";

    std::string result;
    for (const tinyxml2::XMLNode *node = elem->FirstChild(); node; node = node->NextSibling())
    {
        const tinyxml2::XMLText *textNode = node->ToText();
        if (textNode)
        {
            const char *txt = textNode->Value();
            if (txt)
                result += txt;
        }
        else
        {
            const tinyxml2::XMLElement *childElem = node->ToElement();
            if (childElem)
                result += getElementTextValue(childElem, depth + 1);
        }
    }
    return result;
}

/**
 * Helper: Check if an element matches a name (case-sensitive, with namespace prefix fallback).
 * If stepName is "creator", it matches both "creator" and "dc:creator" (bare local name fallback).
 */
static bool nameMatches(const tinyxml2::XMLElement *elem, const std::string &stepName)
{
    if (!elem)
        return false;

    const char *elemName = elem->Name();
    if (!elemName)
        return false;

    // Exact match
    if (stepName == elemName)
        return true;

    // Wildcard match
    if (stepName == "*")
        return true;

    // Fallback: if stepName has no colon, check if elemName's local part (after :) matches.
    if (stepName.find(':') == std::string::npos)
    {
        const char *colon = strchr(elemName, ':');
        if (colon)
        {
            if (stepName == (colon + 1))
                return true;
        }
    }

    return false;
}

/**
 * Helper: strip a matching pair of surrounding quotes from a predicate value.
 */
static std::string trimQuotes(std::string v)
{
    if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"') && v.back() == v.front())
        v = v.substr(1, v.size() - 2);
    return v;
}

/**
 * Helper: split a step into its element name and predicate list.
 * "item[2][@id='x']" -> name "item", predicates {"2", "@id='x'"}.
 * Returns false on unbalanced/garbage brackets, so a malformed query matches nothing.
 */
static bool splitStep(const std::string &step, std::string &name,
                      std::vector<std::string> &predicates)
{
    size_t bracket = step.find('[');
    if (bracket == std::string::npos)
    {
        name = step;
        return true;
    }

    name = step.substr(0, bracket);
    size_t pos = bracket;
    while (pos < step.length())
    {
        if (step[pos] != '[')
            return false; // garbage between predicates
        size_t end = step.find(']', pos);
        if (end == std::string::npos)
            return false; // unbalanced
        predicates.push_back(step.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    return true;
}

/**
 * Helper: evaluate a single step against the current context nodes.
 * A step prefixed with "//" is descendant-or-self, otherwise it selects children.
 * Context is XMLNode (not XMLElement) so the first step can be matched against
 * the document, whose only element child is the root element.
 */
static void evaluateStep(const std::vector<tinyxml2::XMLNode *> &contextNodes,
                         const std::string &stepStr,
                         std::vector<tinyxml2::XMLElement *> &resultNodes)
{
    resultNodes.clear();

    bool isDescendant = false;
    std::string step = stepStr;
    if (step.compare(0, 2, "//") == 0)
    {
        isDescendant = true;
        step = step.substr(2);
    }

    std::string stepName;
    std::vector<std::string> predicates;
    if (!splitStep(step, stepName, predicates) || stepName.empty())
        return;

    // Collect all candidate elements, in document order.
    std::vector<tinyxml2::XMLElement *> candidates;
    for (auto ctx : contextNodes)
    {
        if (ctx == nullptr)
            continue;

        if (isDescendant)
        {
            // Breadth-first, iterative: no recursion to blow the stack on a
            // deeply nested hostile document.
            std::vector<tinyxml2::XMLNode *> queue;
            queue.push_back(ctx);
            for (size_t qidx = 0; qidx < queue.size(); qidx++)
            {
                tinyxml2::XMLNode *curr = queue[qidx];
                tinyxml2::XMLElement *currElem = curr->ToElement();
                if (currElem != nullptr && nameMatches(currElem, stepName))
                    candidates.push_back(currElem);
                for (tinyxml2::XMLElement *child = curr->FirstChildElement(); child;
                     child = child->NextSiblingElement())
                    queue.push_back(child);
            }
        }
        else
        {
            for (tinyxml2::XMLElement *child = ctx->FirstChildElement(); child;
                 child = child->NextSiblingElement())
                if (nameMatches(child, stepName))
                    candidates.push_back(child);
        }
    }

    if (predicates.empty())
    {
        resultNodes = candidates;
        return;
    }

    for (auto candidate : candidates)
    {
        bool matchesAll = true;

        for (const auto &pred : predicates)
        {
            if (pred.empty())
            {
                matchesAll = false;
                break;
            }

            // [n] - 1-based position among the same-named siblings.
            if (isdigit((unsigned char)pred[0]))
            {
                int position = atoi(pred.c_str());
                int count = 0;
                int candidatePos = -1;
                tinyxml2::XMLNode *parent = candidate->Parent();
                for (tinyxml2::XMLElement *sib = parent ? parent->FirstChildElement() : nullptr;
                     sib; sib = sib->NextSiblingElement())
                {
                    if (!nameMatches(sib, stepName))
                        continue;
                    count++;
                    if (sib == candidate)
                        candidatePos = count;
                }
                if (candidatePos != position)
                {
                    matchesAll = false;
                    break;
                }
                continue;
            }

            // [@attr] / [@attr='value']
            if (pred[0] == '@')
            {
                std::string rest = pred.substr(1);
                size_t eq = rest.find('=');
                if (eq == std::string::npos)
                {
                    if (candidate->Attribute(rest.c_str()) == nullptr)
                    {
                        matchesAll = false;
                        break;
                    }
                }
                else
                {
                    std::string attrName = rest.substr(0, eq);
                    std::string want = trimQuotes(rest.substr(eq + 1));
                    const char *have = candidate->Attribute(attrName.c_str());
                    if (have == nullptr || want != have)
                    {
                        matchesAll = false;
                        break;
                    }
                }
                continue;
            }

            // [text()='value']
            if (pred.compare(0, 6, "text()") == 0)
            {
                size_t eq = pred.find('=');
                if (eq == std::string::npos ||
                    getElementTextValue(candidate) != trimQuotes(pred.substr(eq + 1)))
                {
                    matchesAll = false;
                    break;
                }
                continue;
            }

            // Unrecognised predicate: match nothing rather than silently
            // ignoring it and handing back the wrong element.
            matchesAll = false;
            break;
        }

        if (matchesAll)
            resultNodes.push_back(candidate);
    }
}

/**
 * Run the XPath subset query against the parsed document and build the result value.
 *
 * XPath subset supported:
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
 * - Empty/whitespace-only query selects nothing.
 * - Any syntax error selects nothing; a malformed query off the wire never crashes.
 */
bool fnxml_resolve_query(tinyxml2::XMLDocument *doc, const std::string &queryString,
                         size_t matchIndex, std::string &out)
{
    out.clear();

    if (doc == nullptr || doc->RootElement() == nullptr)
        return false;

    std::string query = queryString;

    while (!query.empty() && isspace((unsigned char)query.front()))
        query.erase(0, 1);
    while (!query.empty() && isspace((unsigned char)query.back()))
        query.pop_back();

    if (query.empty())
        return false;

    // Split an optional trailing "@attr" off the query. An '@' inside a
    // predicate (e.g. "item[@id='x']") is an attribute test, not a result
    // selector, so only accept one that sits outside any brackets.
    std::string attr;
    size_t at = query.rfind('@');
    if (at != std::string::npos)
    {
        int bracketDepth = 0;
        for (size_t i = 0; i < at; i++)
        {
            if (query[i] == '[')
                bracketDepth++;
            else if (query[i] == ']')
                bracketDepth--;
        }
        if (bracketDepth == 0)
        {
            attr = query.substr(at + 1);
            query = query.substr(0, at);
            while (!query.empty() && query.back() == ' ')
                query.pop_back();
        }
    }

    if (query.empty())
        return false;

    // Split the path into steps, keeping the '//' marker on descendant steps and
    // ignoring any '/' inside a predicate (e.g. "[@href='http://example.com']").
    std::vector<std::string> steps;
    size_t pos = 0;
    bool descendant = false;
    while (pos < query.length())
    {
        if (query[pos] == '/')
        {
            if (pos + 1 < query.length() && query[pos + 1] == '/')
            {
                descendant = true;
                pos += 2;
            }
            else
            {
                pos++;
            }
            continue;
        }

        size_t start = pos;
        int depth = 0;
        while (pos < query.length())
        {
            char c = query[pos];
            if (c == '[')
                depth++;
            else if (c == ']')
            {
                if (depth > 0)
                    depth--;
            }
            else if (c == '/' && depth == 0)
                break;
            pos++;
        }

        std::string step = query.substr(start, pos - start);
        steps.push_back(descendant ? "//" + step : step);
        descendant = false;
    }

    if (steps.empty())
        return false;

    // The context starts at the document itself, whose only element child is the
    // root element, so the first step matches the root ('/rss/channel/item').
    std::vector<tinyxml2::XMLNode *> currentNodes;
    currentNodes.push_back(doc);

    std::vector<tinyxml2::XMLElement *> matches;
    for (const auto &step : steps)
    {
        evaluateStep(currentNodes, step, matches);
        if (matches.empty())
            return false;
        currentNodes.assign(matches.begin(), matches.end());
    }

    // Return only the current match; an out-of-range index (iteration finished)
    // leaves _value empty so the caller sees 0 bytes available.
    if (matchIndex >= matches.size())
        return false;

    tinyxml2::XMLElement *elem = matches[matchIndex];
    if (attr.empty())
    {
        out = getElementTextValue(elem);
    }
    else
    {
        const char *a = elem->Attribute(attr.c_str());
        if (a != nullptr)
            out = a;
    }
    return true;
}

