/**
 * mail_draft - parsing and validation for composed/replied e-mail.
 *
 * The host writes a message RFC822-style: header lines ("TO: a@b", "CC: ...",
 * "BCC: ...", "SUBJECT: ..."), then the first blank line ends the headers and
 * everything after it is the body, verbatim. CLOSE commits it. This unit turns
 * that text into a validated draft and builds the RFC822 message to send. It
 * depends only on the standard library so the mail_tests target can link it
 * without pulling in Protocol.h -> bus.h.
 *
 * Non-ASCII header/body text passes through as raw UTF-8 (no RFC2047
 * encoded-words); 8-bit hosts emit ASCII, and Gmail accepts UTF-8 headers.
 */

#ifndef MAIL_DRAFT_H
#define MAIL_DRAFT_H

#include <string>

/**
 * What a reply targets. Resolved by the provider at OPEN so later folder
 * renumbering cannot shift it. Any member may be empty; the builder and
 * finalizer degrade gracefully (no Message-ID -> no threading headers,
 * no From/Reply-To -> the host must supply TO).
 */
struct MailReplyTarget
{
    std::string threadId;   // provider thread handle (e.g. Gmail threadId)
    std::string messageId;  // target's RFC5322 Message-ID, angle brackets included
    std::string references; // target's References header, verbatim
    std::string from;       // target's From header
    std::string replyTo;    // target's Reply-To header; preferred over from
    std::string subject;    // target's Subject, as stored (may carry "Re:")
};

struct MailDraft
{
    bool has_to = false;
    bool has_cc = false;
    bool has_bcc = false;
    bool has_subject = false;

    std::string to;      // comma-joined recipient list
    std::string cc;
    std::string bcc;
    std::string subject;
    std::string body;    // verbatim; lines joined with '\n'
};

enum class MailDraftError
{
    NONE,
    EMPTY,      // no header field and a whitespace-only body
    BAD_LINE,   // non-blank, colon-less line in the header section
    BAD_KEY,    // unknown header key (typos must not silently drop recipients)
    MISSING_TO, // no recipient after finalize (compose, or reply with no default)
};

/**
 * Parse the written text into a draft. Lines end with any of 0x9B (ATASCII),
 * CR, LF or CRLF. Header keys are case-insensitive; keys and values are
 * trimmed. TO/CC/BCC append on repeat with ", "; SUBJECT is last-wins. The
 * first blank line is consumed and everything after it is body, untouched --
 * colons and header-lookalikes there are body text. A draft with no blank
 * line has an empty body; a leading blank line makes the whole draft body.
 */
MailDraftError mail_draft_parse(const std::string &raw, MailDraft &out);

/**
 * Validate the draft and apply reply defaults. reply == nullptr means compose.
 * For a reply, an omitted TO defaults to the target's Reply-To (else From) and
 * an omitted SUBJECT to mail_reply_subject(target subject); host-written
 * values win untouched. Both modes then require a non-empty TO.
 */
MailDraftError mail_draft_finalize(MailDraft &d, const MailReplyTarget *reply);

/**
 * "Re: <orig>", unless orig already starts with "re:" case-insensitively.
 */
std::string mail_reply_subject(const std::string &orig);

/**
 * Build the RFC822 message (CRLF line endings) for a finalized draft. From
 * and Date are omitted on purpose: the provider sets both from the
 * authenticated account, which also forecloses sender spoofing. For a reply,
 * In-Reply-To/References are emitted only when the target has a Message-ID.
 */
std::string mail_draft_rfc822(const MailDraft &d, const MailReplyTarget *reply);

#endif /* MAIL_DRAFT_H */
