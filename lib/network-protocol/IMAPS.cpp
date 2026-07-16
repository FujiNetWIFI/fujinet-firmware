/**
 * NetworkProtocolIMAPS - IMAP-over-TLS mailbox adapter.
 *
 * Uses fnTcpClientSecure for the TLS transport, a small literal-aware parser for
 * the IMAP data grammar (reused for ENVELOPE and BODYSTRUCTURE), and BODYSTRUCTURE
 * walking to select the body part and enumerate attachments.
 */

#include "IMAPS.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "../../include/debug.h"
#include "../encoding/base64.h"

namespace {

// ─── small string helpers ─────────────────────────────────────────────────────

bool ci_equal(const std::string &a, const char *b)
{
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

std::string upper(const std::string &s)
{
    std::string o = s;
    for (auto &c : o) c = (char)toupper((unsigned char)c);
    return o;
}

// IMAP quoted string with backslash escaping of " and \.
std::string imap_quote(const std::string &s)
{
    std::string o = "\"";
    for (char c : s)
    {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    o += "\"";
    return o;
}

// True if `seg` ends with an IMAP literal spec "{n}" (or "{n+}"); sets n.
bool trailing_literal(const std::string &seg, size_t &n)
{
    if (seg.empty() || seg.back() != '}') return false;
    size_t br = seg.rfind('{');
    if (br == std::string::npos) return false;
    std::string inner = seg.substr(br + 1, seg.size() - br - 2);
    if (inner.empty()) return false;
    size_t len = inner.size();
    if (inner.back() == '+') { if (len == 1) return false; len--; }
    for (size_t i = 0; i < len; i++)
        if (!isdigit((unsigned char)inner[i])) return false;
    n = (size_t)strtoul(inner.c_str(), nullptr, 10);
    return true;
}

// ─── transfer-encoding / header-word decoders ─────────────────────────────────

std::string base64_decode(const std::string &in)
{
    std::string clean;
    clean.reserve(in.size());
    for (char c : in)
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') clean += c;
    if (clean.empty()) return "";
    size_t outlen = 0;
    auto p = Base64::decode(clean.c_str(), clean.size(), &outlen);
    if (!p) return "";
    return std::string((char *)p.get(), outlen);
}

int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string qp_decode(const std::string &in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); i++)
    {
        char c = in[i];
        if (c == '=')
        {
            if (i + 2 < in.size() && hexval(in[i + 1]) >= 0 && hexval(in[i + 2]) >= 0)
            {
                out += (char)((hexval(in[i + 1]) << 4) | hexval(in[i + 2]));
                i += 2;
            }
            else if (i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n')
                i += 2; // soft line break
            else if (i + 1 < in.size() && in[i + 1] == '\n')
                i += 1;
            // otherwise drop a stray '='
        }
        else
            out += c;
    }
    return out;
}

std::string decode_transfer(const std::string &enc, const std::string &data)
{
    if (ci_equal(enc, "BASE64")) return base64_decode(data);
    if (ci_equal(enc, "QUOTED-PRINTABLE")) return qp_decode(data);
    return data;
}

// Decode RFC 2047 encoded-words ("=?charset?B?..?=" / "=?charset?Q?..?=").
std::string decode_rfc2047(const std::string &in)
{
    std::string out;
    size_t i = 0;
    while (i < in.size())
    {
        size_t s = in.find("=?", i);
        if (s == std::string::npos) { out += in.substr(i); break; }
        out += in.substr(i, s - i);
        size_t c1 = in.find('?', s + 2);
        size_t c2 = (c1 == std::string::npos) ? std::string::npos : in.find('?', c1 + 1);
        size_t end = (c2 == std::string::npos) ? std::string::npos : in.find("?=", c2 + 1);
        if (end == std::string::npos) { out += in.substr(s); break; }
        char enc = (char)toupper((unsigned char)in[c1 + 1]);
        std::string text = in.substr(c2 + 1, end - (c2 + 1));
        if (enc == 'B')
            out += base64_decode(text);
        else if (enc == 'Q')
        {
            std::string t;
            for (char ch : text) t += (ch == '_') ? ' ' : ch;
            out += qp_decode(t);
        }
        else
            out += text;
        i = end + 2;
    }
    return out;
}

// ─── INTERNALDATE parsing ─────────────────────────────────────────────────────

int month_num(const char *m)
{
    static const char *names[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                  "jul", "aug", "sep", "oct", "nov", "dec"};
    char lm[3] = {(char)tolower((unsigned char)m[0]), (char)tolower((unsigned char)m[1]),
                  (char)tolower((unsigned char)m[2])};
    for (int i = 0; i < 12; i++)
        if (lm[0] == names[i][0] && lm[1] == names[i][1] && lm[2] == names[i][2]) return i + 1;
    return 1;
}

int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// "17-Jul-2026 13:45:00 +0000" -> epoch seconds (UTC).
uint64_t parse_internaldate(const std::string &s)
{
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char mon[8] = {0}, tz[8] = {0};
    if (sscanf(s.c_str(), "%d-%3s-%d %d:%d:%d %5s", &day, mon, &year, &hh, &mm, &ss, tz) < 6)
        return 0;
    int64_t secs = days_from_civil(year, month_num(mon), day) * 86400 + hh * 3600 + mm * 60 + ss;
    if (tz[0] == '+' || tz[0] == '-')
    {
        int v = atoi(tz + 1);
        int off = (v / 100) * 3600 + (v % 100) * 60;
        secs -= (tz[0] == '-' ? -off : off);
    }
    return secs < 0 ? 0 : (uint64_t)secs;
}

// ─── IMAP value parser (quoted / literal / list / atom / NIL) ──────────────────

struct ImapNode
{
    enum Type { LIST, STRING, ATOM, NIL } type = ATOM;
    std::string str;
    std::vector<ImapNode> items;
};

class ImapParser
{
public:
    explicit ImapParser(const std::string &s) : _s(s), _p(0) {}

    // Advance to the next "* <n> FETCH (...)" record. Returns false at end/tag.
    bool nextFetch(uint32_t &seq, ImapNode &node)
    {
        for (;;)
        {
            skipWs();
            if (_p >= _s.size() || _s[_p] != '*') return false;
            _p++;
            skipSp();
            size_t st = _p;
            while (_p < _s.size() && isdigit((unsigned char)_s[_p])) _p++;
            if (_p == st) { skipLine(); continue; }
            uint32_t num = (uint32_t)strtoul(_s.substr(st, _p - st).c_str(), nullptr, 10);
            skipSp();
            size_t ws = _p;
            while (_p < _s.size() && isalpha((unsigned char)_s[_p])) _p++;
            std::string word = _s.substr(ws, _p - ws);
            if (!ci_equal(word, "FETCH")) { skipLine(); continue; }
            skipSp();
            if (_p >= _s.size() || _s[_p] != '(') { skipLine(); continue; }
            node = parseValue();
            seq = num;
            return true;
        }
    }

private:
    const std::string &_s;
    size_t _p;

    void skipWs() { while (_p < _s.size() && (_s[_p] == '\r' || _s[_p] == '\n' || _s[_p] == ' ' || _s[_p] == '\t')) _p++; }
    void skipSp() { while (_p < _s.size() && (_s[_p] == ' ' || _s[_p] == '\t')) _p++; }
    void skipLine() { while (_p < _s.size() && _s[_p] != '\n') _p++; if (_p < _s.size()) _p++; }

    ImapNode parseValue()
    {
        skipWs();
        if (_p >= _s.size()) return ImapNode();
        char c = _s[_p];
        if (c == '(') return parseList();
        if (c == '"') return parseQuoted();
        if (c == '{') return parseLiteral();
        return parseAtom();
    }

    ImapNode parseList()
    {
        ImapNode n;
        n.type = ImapNode::LIST;
        _p++; // '('
        for (;;)
        {
            skipWs();
            if (_p >= _s.size()) break;
            if (_s[_p] == ')') { _p++; break; }
            n.items.push_back(parseValue());
        }
        return n;
    }

    ImapNode parseQuoted()
    {
        ImapNode n;
        n.type = ImapNode::STRING;
        _p++; // '"'
        while (_p < _s.size())
        {
            char c = _s[_p++];
            if (c == '\\' && _p < _s.size()) { n.str += _s[_p++]; continue; }
            if (c == '"') break;
            n.str += c;
        }
        return n;
    }

    ImapNode parseLiteral()
    {
        ImapNode n;
        n.type = ImapNode::STRING;
        _p++; // '{'
        size_t st = _p;
        while (_p < _s.size() && isdigit((unsigned char)_s[_p])) _p++;
        size_t len = (size_t)strtoul(_s.substr(st, _p - st).c_str(), nullptr, 10);
        if (_p < _s.size() && _s[_p] == '+') _p++;
        if (_p < _s.size() && _s[_p] == '}') _p++;
        if (_p < _s.size() && _s[_p] == '\r') _p++;
        if (_p < _s.size() && _s[_p] == '\n') _p++;
        if (_p + len <= _s.size()) { n.str = _s.substr(_p, len); _p += len; }
        else { n.str = _s.substr(_p); _p = _s.size(); }
        return n;
    }

    ImapNode parseAtom()
    {
        ImapNode n;
        n.type = ImapNode::ATOM;
        size_t st = _p;
        while (_p < _s.size())
        {
            char c = _s[_p];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '(' || c == ')' || c == '"' || c == '{') break;
            _p++;
        }
        n.str = _s.substr(st, _p - st);
        if (ci_equal(n.str, "NIL")) n.type = ImapNode::NIL;
        return n;
    }
};

// ─── BODYSTRUCTURE walking ────────────────────────────────────────────────────

struct MimePart
{
    std::string partNum, type, subtype, encoding, name, filename;
    uint64_t size = 0;
};

void walk_bodystructure(const ImapNode &node, const std::string &prefix, std::vector<MimePart> &out)
{
    if (node.type != ImapNode::LIST || node.items.empty()) return;

    if (node.items[0].type == ImapNode::LIST)
    {
        // multipart: leading LIST items are the child parts
        int idx = 1;
        for (const auto &child : node.items)
        {
            if (child.type != ImapNode::LIST) break;
            std::string pn = prefix.empty() ? std::to_string(idx) : prefix + "." + std::to_string(idx);
            walk_bodystructure(child, pn, out);
            idx++;
        }
        return;
    }

    // leaf part
    const auto &it = node.items;
    MimePart p;
    p.partNum = prefix.empty() ? "1" : prefix;
    if (it.size() > 0 && it[0].type == ImapNode::STRING) p.type = upper(it[0].str);
    if (it.size() > 1 && it[1].type == ImapNode::STRING) p.subtype = upper(it[1].str);
    if (it.size() > 2 && it[2].type == ImapNode::LIST)
    {
        const auto &pl = it[2].items;
        for (size_t k = 0; k + 1 < pl.size(); k += 2)
            if (ci_equal(pl[k].str, "NAME")) p.name = decode_rfc2047(pl[k + 1].str);
    }
    if (it.size() > 5 && it[5].type == ImapNode::STRING) p.encoding = upper(it[5].str);
    if (it.size() > 6) p.size = (uint64_t)strtoull(it[6].str.c_str(), nullptr, 10);

    // disposition (filename) appears among the extension items after index 7
    for (size_t k = 7; k < it.size(); k++)
    {
        if (it[k].type == ImapNode::LIST && it[k].items.size() >= 2 &&
            it[k].items[0].type == ImapNode::STRING && it[k].items[1].type == ImapNode::LIST)
        {
            std::string dt = upper(it[k].items[0].str);
            if (dt == "ATTACHMENT" || dt == "INLINE")
            {
                const auto &dp = it[k].items[1].items;
                for (size_t j = 0; j + 1 < dp.size(); j += 2)
                    if (ci_equal(dp[j].str, "FILENAME")) p.filename = decode_rfc2047(dp[j + 1].str);
            }
        }
    }
    out.push_back(p);
}

void parse_bodystructure_leaves(const std::string &full, std::vector<MimePart> &leaves)
{
    ImapParser parser(full);
    uint32_t seq;
    ImapNode node;
    if (!parser.nextFetch(seq, node)) return;
    for (size_t i = 0; i + 1 < node.items.size(); i += 2)
        if (ci_equal(node.items[i].str, "BODYSTRUCTURE") || ci_equal(node.items[i].str, "BODY"))
        {
            walk_bodystructure(node.items[i + 1], "", leaves);
            return;
        }
}

int select_body_index(const std::vector<MimePart> &leaves)
{
    for (size_t i = 0; i < leaves.size(); i++)
        if (leaves[i].type == "TEXT" && leaves[i].subtype == "PLAIN") return (int)i;
    for (size_t i = 0; i < leaves.size(); i++)
        if (leaves[i].type == "TEXT" && leaves[i].subtype == "HTML") return (int)i;
    return leaves.empty() ? -1 : 0;
}

std::string mime_str(const MimePart &p)
{
    std::string t = p.type, s = p.subtype;
    for (auto &c : t) c = (char)tolower((unsigned char)c);
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    if (t.empty()) return "application/octet-stream";
    return t + "/" + s;
}

std::string part_filename(const MimePart &p)
{
    if (!p.filename.empty()) return p.filename;
    if (!p.name.empty()) return p.name;
    return "part " + p.partNum;
}

} // namespace

// ─── construction ─────────────────────────────────────────────────────────────

NetworkProtocolIMAPS::NetworkProtocolIMAPS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolMailbox(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolIMAPS::ctor\r\n");
}

NetworkProtocolIMAPS::~NetworkProtocolIMAPS()
{
    Debug_printf("NetworkProtocolIMAPS::dtor\r\n");
    if (_tls.connected())
    {
        std::string full;
        do_command("LOGOUT", full);
    }
    _tls.stop();
}

// ─── IMAP command plumbing ────────────────────────────────────────────────────

NetworkProtocolIMAPS::ImapStatus NetworkProtocolIMAPS::read_raw_response(const std::string &tag,
                                                                        std::string &full)
{
    full.clear();
    for (;;)
    {
        std::string line;
        if (!_tls.readLine(line, 20000)) return IMAP_TIMEOUT;
        full += line;
        full += "\r\n";

        size_t n;
        if (trailing_literal(line, n))
        {
            std::string lit;
            if (!_tls.readN(lit, n, 30000)) return IMAP_TIMEOUT;
            full += lit;
            continue; // literal is followed by more of the response
        }

        std::string pfx = tag + " ";
        if (line.compare(0, pfx.size(), pfx) == 0)
        {
            std::string rest = line.substr(pfx.size());
            size_t p = rest.find_first_not_of(' ');
            if (p != std::string::npos) rest = rest.substr(p);
            if (rest.compare(0, 2, "OK") == 0) return IMAP_OK;
            if (rest.compare(0, 2, "NO") == 0) return IMAP_NO;
            return IMAP_BAD;
        }
    }
}

NetworkProtocolIMAPS::ImapStatus NetworkProtocolIMAPS::do_command(const std::string &cmd,
                                                                  std::string &full)
{
    if (!_tls.connected()) return IMAP_TIMEOUT;
    char tagbuf[16];
    snprintf(tagbuf, sizeof(tagbuf), "A%04d", ++_tag);
    std::string tag = tagbuf;
    _tls.write(tag + " " + cmd + "\r\n");
    return read_raw_response(tag, full);
}

NetworkProtocolIMAPS::ImapStatus NetworkProtocolIMAPS::select_folder(const std::string &folder)
{
    std::string full;
    ImapStatus st = do_command("SELECT " + imap_quote(folder), full);
    if (st != IMAP_OK) return st;

    _selectedCount = 0;
    std::istringstream ss(full);
    std::string line;
    while (std::getline(ss, line))
        if (!line.empty() && line[0] == '*' && line.find(" EXISTS") != std::string::npos)
            _selectedCount = (uint32_t)strtoul(line.c_str() + 1, nullptr, 10);
    _selectedFolder = folder;
    return IMAP_OK;
}

bool NetworkProtocolIMAPS::fetch_section(uint32_t seq, const std::string &section, std::string &rawOut)
{
    std::string full;
    if (do_command("FETCH " + std::to_string(seq) + " BODY[" + section + "]", full) != IMAP_OK)
        return false;
    ImapParser parser(full);
    uint32_t s;
    ImapNode node;
    if (!parser.nextFetch(s, node)) return false;
    for (size_t i = 0; i + 1 < node.items.size(); i += 2)
        if (node.items[i].str.compare(0, 5, "BODY[") == 0)
        {
            const ImapNode &v = node.items[i + 1];
            rawOut = (v.type == ImapNode::STRING) ? v.str : "";
            return true;
        }
    return false;
}

// ─── mailbox provider hooks ───────────────────────────────────────────────────

fujiError_t NetworkProtocolIMAPS::connect_and_auth()
{
    _host = opened_url->host;
    uint16_t p = opened_url->getPort();
    _port = p ? p : 993;
    _user = !opened_url->user.empty() ? opened_url->user
                                      : (login && !login->empty() ? *login : "");
    _pass = !opened_url->password.empty() ? opened_url->password
                                          : (password && !password->empty() ? *password : "");

    if (_host.empty()) { _lastErr = NDEV_STATUS::INVALID_DEVICESPEC; return FUJI_ERROR::UNSPECIFIED; }
    if (_user.empty()) { _lastErr = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD; return FUJI_ERROR::UNSPECIFIED; }

    if (!_tls.connect(_host, _port, 15000))
    {
        _lastErr = NDEV_STATUS::CONNECTION_REFUSED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::string greeting;
    if (!_tls.readLine(greeting, 15000))
    {
        _lastErr = NDEV_STATUS::SOCKET_TIMEOUT;
        return FUJI_ERROR::UNSPECIFIED;
    }
    if (greeting.rfind("* PREAUTH", 0) == 0)
    {
        _loggedIn = true;
        return FUJI_ERROR::NONE;
    }
    if (greeting.rfind("* OK", 0) != 0)
    {
        _lastErr = NDEV_STATUS::SERVICE_NOT_AVAILABLE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::string full;
    if (do_command("LOGIN " + imap_quote(_user) + " " + imap_quote(_pass), full) != IMAP_OK)
    {
        _lastErr = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        return FUJI_ERROR::UNSPECIFIED;
    }
    _loggedIn = true;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolIMAPS::folder_count(const std::string &folder, uint32_t &count)
{
    if (select_folder(folder) != IMAP_OK) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }
    count = _selectedCount;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolIMAPS::folder_index(const std::string &folder, long rangeStart, long rangeEnd,
                                               bool newest, std::vector<MailboxIndexEntry> &out)
{
    if (select_folder(folder) != IMAP_OK) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }
    uint32_t total = _selectedCount;
    if (total == 0) return FUJI_ERROR::NONE;

    long maxIdx = (long)total - 1;
    if (rangeStart > maxIdx) return FUJI_ERROR::NONE;
    if (rangeEnd > maxIdx) rangeEnd = maxIdx;

    uint32_t seqLo = total - (uint32_t)rangeEnd; // newest-first index -> seq number
    uint32_t seqHi = total - (uint32_t)rangeStart;
    std::string seqset = (seqLo == seqHi) ? std::to_string(seqLo)
                                          : std::to_string(seqLo) + ":" + std::to_string(seqHi);

    std::string full;
    if (do_command("FETCH " + seqset + " (FLAGS INTERNALDATE ENVELOPE)", full) != IMAP_OK)
    {
        _lastErr = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    ImapParser parser(full);
    uint32_t seq;
    ImapNode node;
    while (parser.nextFetch(seq, node))
    {
        MailboxIndexEntry e;
        e.msgNum = seq;
        for (size_t i = 0; i + 1 < node.items.size(); i += 2)
        {
            const std::string &key = node.items[i].str;
            const ImapNode &val = node.items[i + 1];
            if (ci_equal(key, "FLAGS") && val.type == ImapNode::LIST)
            {
                for (const auto &f : val.items)
                    if (ci_equal(f.str, "\\Flagged")) e.important = true;
            }
            else if (ci_equal(key, "INTERNALDATE") && val.type == ImapNode::STRING)
                e.timestamp = parse_internaldate(val.str);
            else if (ci_equal(key, "ENVELOPE") && val.type == ImapNode::LIST)
            {
                const auto &env = val.items;
                if (env.size() > 1 && env[1].type == ImapNode::STRING)
                    e.subject = decode_rfc2047(env[1].str);
                if (env.size() > 2 && env[2].type == ImapNode::LIST && !env[2].items.empty())
                {
                    const ImapNode &addr = env[2].items[0];
                    if (addr.type == ImapNode::LIST && addr.items.size() >= 4)
                    {
                        if (addr.items[0].type == ImapNode::STRING)
                            e.displayName = decode_rfc2047(addr.items[0].str);
                        std::string mbox = addr.items[2].type == ImapNode::STRING ? addr.items[2].str : "";
                        std::string host = addr.items[3].type == ImapNode::STRING ? addr.items[3].str : "";
                        if (!mbox.empty() && !host.empty()) e.emailAddress = mbox + "@" + host;
                    }
                }
            }
        }
        out.push_back(e);
    }

    std::sort(out.begin(), out.end(),
              [](const MailboxIndexEntry &a, const MailboxIndexEntry &b) { return a.msgNum > b.msgNum; });
    if (!newest) std::reverse(out.begin(), out.end());
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolIMAPS::message_body(const std::string &folder, uint32_t seq, std::string &out)
{
    if (select_folder(folder) != IMAP_OK) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }
    if (seq < 1 || seq > _selectedCount) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }

    std::string bs;
    if (do_command("FETCH " + std::to_string(seq) + " BODYSTRUCTURE", bs) != IMAP_OK)
    {
        _lastErr = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    std::vector<MimePart> leaves;
    parse_bodystructure_leaves(bs, leaves);

    std::string section = "TEXT", enc;
    int bi = select_body_index(leaves);
    if (bi >= 0) { section = leaves[bi].partNum; enc = leaves[bi].encoding; }

    std::string raw;
    if (!fetch_section(seq, section, raw)) { _lastErr = NDEV_STATUS::GENERAL; return FUJI_ERROR::UNSPECIFIED; }
    out = decode_transfer(enc, raw);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolIMAPS::attachment_index(const std::string &folder, uint32_t seq,
                                                   std::vector<MailboxAttachmentEntry> &out)
{
    if (select_folder(folder) != IMAP_OK) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }
    if (seq < 1 || seq > _selectedCount) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }

    std::string bs;
    if (do_command("FETCH " + std::to_string(seq) + " BODYSTRUCTURE", bs) != IMAP_OK)
    {
        _lastErr = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    std::vector<MimePart> leaves;
    parse_bodystructure_leaves(bs, leaves);
    int bi = select_body_index(leaves);

    MailboxAttachmentEntry body;
    body.attachmentNum = 0;
    body.displayName = "body";
    if (bi >= 0) { body.mimeType = mime_str(leaves[bi]); body.length = leaves[bi].size; }
    out.push_back(body);

    uint8_t n = 1;
    for (int i = 0; i < (int)leaves.size(); i++)
    {
        if (i == bi) continue;
        MailboxAttachmentEntry e;
        e.attachmentNum = n++;
        e.fileName = part_filename(leaves[i]);
        e.displayName = e.fileName;
        e.mimeType = mime_str(leaves[i]);
        e.length = leaves[i].size;
        out.push_back(e);
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolIMAPS::attachment_data(const std::string &folder, uint32_t seq,
                                                  uint8_t attach, std::string &out)
{
    if (select_folder(folder) != IMAP_OK) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }
    if (seq < 1 || seq > _selectedCount) { _lastErr = NDEV_STATUS::FILE_NOT_FOUND; return FUJI_ERROR::UNSPECIFIED; }

    std::string bs;
    if (do_command("FETCH " + std::to_string(seq) + " BODYSTRUCTURE", bs) != IMAP_OK)
    {
        _lastErr = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    std::vector<MimePart> leaves;
    parse_bodystructure_leaves(bs, leaves);
    int bi = select_body_index(leaves);

    const MimePart *target = nullptr;
    if (attach == 0)
    {
        if (bi >= 0) target = &leaves[bi];
    }
    else
    {
        int count = 0;
        for (int i = 0; i < (int)leaves.size(); i++)
        {
            if (i == bi) continue;
            if (++count == attach) { target = &leaves[i]; break; }
        }
    }
    if (!target)
    {
        // single-part message with no BODYSTRUCTURE parts: fall back to whole text
        if (attach == 0)
        {
            std::string raw;
            if (fetch_section(seq, "TEXT", raw)) { out = raw; return FUJI_ERROR::NONE; }
        }
        _lastErr = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::string raw;
    if (!fetch_section(seq, target->partNum, raw)) { _lastErr = NDEV_STATUS::GENERAL; return FUJI_ERROR::UNSPECIFIED; }
    out = decode_transfer(target->encoding, raw);
    return FUJI_ERROR::NONE;
}

void NetworkProtocolIMAPS::mailbox_error_to_error()
{
    error = _lastErr;
}
