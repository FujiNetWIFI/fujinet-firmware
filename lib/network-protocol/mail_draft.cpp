#include "mail_draft.h"

#include <cctype>
#include <vector>

namespace
{

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::string upper(std::string s)
{
    for (auto &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// Split on 0x9B / CR / LF / CRLF so every platform's native EOL works.
std::vector<std::string> split_lines(const std::string &raw)
{
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < raw.size(); i++)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0x9B || c == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else if (c == '\r')
        {
            if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
            lines.push_back(cur);
            cur.clear();
        }
        else
            cur += raw[i];
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

void append_recipients(std::string &dst, const std::string &val, bool &has)
{
    has = true;
    if (val.empty()) return;
    if (dst.empty())
        dst = val;
    else
        dst += ", " + val;
}

} // namespace

MailDraftError mail_draft_parse(const std::string &raw, MailDraft &out)
{
    std::vector<std::string> lines = split_lines(raw);

    size_t i = 0;
    for (; i < lines.size(); i++)
    {
        std::string l = trim(lines[i]);
        if (l.empty())
        {
            i++; // the first blank line ends the headers and is consumed
            break;
        }

        size_t colon = l.find(':');
        if (colon == std::string::npos)
            return MailDraftError::BAD_LINE;

        std::string key = upper(trim(l.substr(0, colon)));
        std::string val = trim(l.substr(colon + 1));

        if (key == "TO")
            append_recipients(out.to, val, out.has_to);
        else if (key == "CC")
            append_recipients(out.cc, val, out.has_cc);
        else if (key == "BCC")
            append_recipients(out.bcc, val, out.has_bcc);
        else if (key == "SUBJECT")
        {
            out.subject = val;
            out.has_subject = true;
        }
        else
            return MailDraftError::BAD_KEY;
    }

    // Everything after the blank line is body, verbatim.
    for (bool first = true; i < lines.size(); i++, first = false)
    {
        if (!first) out.body += '\n';
        out.body += lines[i];
    }

    return MailDraftError::NONE;
}

std::string mail_reply_subject(const std::string &orig)
{
    std::string t = trim(orig);
    if (t.empty()) return "Re:";
    if (t.size() >= 3 && upper(t.substr(0, 3)) == "RE:") return t;
    return "Re: " + t;
}

MailDraftError mail_draft_finalize(MailDraft &d, const MailReplyTarget *reply)
{
    bool anyHeader = d.has_to || d.has_cc || d.has_bcc || d.has_subject;
    if (!anyHeader && d.body.find_first_not_of(" \t\r\n") == std::string::npos)
        return MailDraftError::EMPTY;

    if (reply)
    {
        if (!d.has_to)
        {
            d.to = !reply->replyTo.empty() ? reply->replyTo : reply->from;
            d.has_to = !d.to.empty();
        }
        if (!d.has_subject)
        {
            d.subject = mail_reply_subject(reply->subject);
            d.has_subject = true;
        }
    }

    if (!d.has_to || d.to.empty())
        return MailDraftError::MISSING_TO;

    return MailDraftError::NONE;
}

std::string mail_draft_rfc822(const MailDraft &d, const MailReplyTarget *reply)
{
    std::string m;
    m += "To: " + d.to + "\r\n";
    if (d.has_cc && !d.cc.empty())
        m += "Cc: " + d.cc + "\r\n";
    if (d.has_bcc && !d.bcc.empty())
        m += "Bcc: " + d.bcc + "\r\n"; // the provider strips Bcc on delivery
    if (d.has_subject)
        m += "Subject: " + d.subject + "\r\n";
    if (reply && !reply->messageId.empty())
    {
        m += "In-Reply-To: " + reply->messageId + "\r\n";
        m += "References: " + (reply->references.empty()
                                   ? reply->messageId
                                   : reply->references + " " + reply->messageId) + "\r\n";
    }
    m += "MIME-Version: 1.0\r\n";
    m += "Content-Type: text/plain; charset=utf-8\r\n";
    m += "\r\n";
    // The body contains only '\n' by construction (parse joins with it).
    for (char c : d.body)
    {
        if (c == '\n') m += "\r\n";
        else m += c;
    }
    return m;
}
