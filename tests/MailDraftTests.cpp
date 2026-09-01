#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "network-protocol/mail_draft.h"

#include <string>

namespace
{

MailDraft parsed(const std::string &raw)
{
    MailDraft d;
    REQUIRE(mail_draft_parse(raw, d) == MailDraftError::NONE);
    return d;
}

} // namespace

// ─── parsing: EOL forms ───────────────────────────────────────────────────────

TEST_CASE("draft parsing accepts every platform EOL")
{
    const std::string eols[] = {"\n", "\r\n", "\r", "\x9b"};
    for (const auto &e : eols)
    {
        CAPTURE(e);
        MailDraft d = parsed("TO: a@b" + e + "SUBJECT: Hi" + e + e + "line1" + e + "line2" + e);
        CHECK(d.to == "a@b");
        CHECK(d.subject == "Hi");
        CHECK(d.body == "line1\nline2");
    }

    // Mixed EOLs in one draft; the blank-line boundary in yet another form.
    MailDraft d = parsed("TO: a@b\x9bSUBJECT: Hi\r\n\rbody line");
    CHECK(d.to == "a@b");
    CHECK(d.subject == "Hi");
    CHECK(d.body == "body line");
}

// ─── parsing: header rules ────────────────────────────────────────────────────

TEST_CASE("header keys and values")
{
    SUBCASE("case-insensitive keys, trimmed keys and values")
    {
        MailDraft d = parsed("to :  a@b  \n Subject :  Hello \n");
        CHECK(d.to == "a@b");
        CHECK(d.subject == "Hello");
        CHECK(d.body.empty());
    }
    SUBCASE("TO/CC/BCC append on repeat; SUBJECT last-wins")
    {
        MailDraft d = parsed("TO: a@b\nTO: c@d\nCC: e@f\nCC: g@h\nSUBJECT: one\nSUBJECT: two\n");
        CHECK(d.to == "a@b, c@d");
        CHECK(d.cc == "e@f, g@h");
        CHECK(d.subject == "two");
    }
    SUBCASE("unknown key and colon-less line are rejected")
    {
        MailDraft d;
        CHECK(mail_draft_parse("FROM: spoof@x\n", d) == MailDraftError::BAD_KEY);
        MailDraft d2;
        CHECK(mail_draft_parse("TO a@b\n", d2) == MailDraftError::BAD_LINE);
    }
    SUBCASE("an EOL can never land inside a header value")
    {
        MailDraft d;
        CHECK(mail_draft_parse("TO: a@b\x9bX-Evil: 1\x9b\x9b" "body", d) == MailDraftError::BAD_KEY);
        CHECK(d.to == "a@b"); // lines were split before values existed
    }
}

// ─── parsing: body is verbatim ────────────────────────────────────────────────

TEST_CASE("body is verbatim")
{
    SUBCASE("colons and header lookalikes stay body")
    {
        MailDraft d = parsed("TO: a@b\n\nNote: 3:00 PM\nTO: not-a-header\n");
        CHECK(d.body == "Note: 3:00 PM\nTO: not-a-header");
    }
    SUBCASE("interior blank lines are preserved")
    {
        MailDraft d = parsed("TO: a@b\n\npara one\n\npara two\n");
        CHECK(d.body == "para one\n\npara two");
    }
    SUBCASE("no blank line -> headers only, empty body")
    {
        MailDraft d = parsed("TO: a@b\nSUBJECT: hi\n");
        CHECK(d.body.empty());
    }
    SUBCASE("leading blank line -> the whole draft is body")
    {
        MailDraft d = parsed("\nTO: still body\n");
        CHECK(!d.has_to);
        CHECK(d.body == "TO: still body");
    }
}

// ─── finalize ─────────────────────────────────────────────────────────────────

TEST_CASE("finalize: compose")
{
    SUBCASE("TO is required")
    {
        MailDraft d = parsed("SUBJECT: hi\n\nbody\n");
        CHECK(mail_draft_finalize(d, nullptr) == MailDraftError::MISSING_TO);
    }
    SUBCASE("TO alone with an empty body is legal")
    {
        MailDraft d = parsed("TO: a@b\n");
        CHECK(mail_draft_finalize(d, nullptr) == MailDraftError::NONE);
    }
    SUBCASE("a whitespace-only draft is EMPTY in both modes")
    {
        MailDraft d = parsed("\n \n\n");
        CHECK(mail_draft_finalize(d, nullptr) == MailDraftError::EMPTY);

        MailDraft r = parsed("\n\n");
        MailReplyTarget t;
        t.from = "orig@x";
        CHECK(mail_draft_finalize(r, &t) == MailDraftError::EMPTY);
    }
}

TEST_CASE("finalize: reply defaults")
{
    MailReplyTarget t;
    t.threadId = "t1";
    t.messageId = "<mid@x>";
    t.from = "Alice <alice@x>";
    t.replyTo = "list@x";
    t.subject = "Meeting";

    SUBCASE("omitted TO and SUBJECT default from the target")
    {
        MailDraft d = parsed("\nThanks!\n");
        REQUIRE(mail_draft_finalize(d, &t) == MailDraftError::NONE);
        CHECK(d.to == "list@x"); // Reply-To preferred over From
        CHECK(d.subject == "Re: Meeting");
    }
    SUBCASE("From is the fallback when Reply-To is absent")
    {
        MailReplyTarget t2 = t;
        t2.replyTo.clear();
        MailDraft d = parsed("\nThanks!\n");
        REQUIRE(mail_draft_finalize(d, &t2) == MailDraftError::NONE);
        CHECK(d.to == "Alice <alice@x>");
    }
    SUBCASE("host-written values override the defaults")
    {
        MailDraft d = parsed("TO: other@y\nSUBJECT: New topic\n\nhi\n");
        REQUIRE(mail_draft_finalize(d, &t) == MailDraftError::NONE);
        CHECK(d.to == "other@y");
        CHECK(d.subject == "New topic");
    }
    SUBCASE("a target with no From or Reply-To still needs a TO")
    {
        MailReplyTarget t2;
        t2.subject = "x";
        MailDraft d = parsed("\nhello\n");
        CHECK(mail_draft_finalize(d, &t2) == MailDraftError::MISSING_TO);
    }
}

TEST_CASE("reply subject Re: handling")
{
    CHECK(mail_reply_subject("hi") == "Re: hi");
    CHECK(mail_reply_subject("Re: hi") == "Re: hi");
    CHECK(mail_reply_subject("RE: hi") == "RE: hi");
    CHECK(mail_reply_subject("re: hi") == "re: hi");
    CHECK(mail_reply_subject("Reply: hi") == "Re: Reply: hi");
    CHECK(mail_reply_subject("") == "Re:");
    CHECK(mail_reply_subject("  hi  ") == "Re: hi");
}

// ─── RFC822 builder ───────────────────────────────────────────────────────────

TEST_CASE("rfc822 builder: compose")
{
    MailDraft d = parsed("TO: a@b\nCC: c@d\nBCC: e@f\nSUBJECT: Hi\n\nline1\nline2\n");
    REQUIRE(mail_draft_finalize(d, nullptr) == MailDraftError::NONE);
    std::string m = mail_draft_rfc822(d, nullptr);
    CHECK(m == "To: a@b\r\n"
               "Cc: c@d\r\n"
               "Bcc: e@f\r\n"
               "Subject: Hi\r\n"
               "MIME-Version: 1.0\r\n"
               "Content-Type: text/plain; charset=utf-8\r\n"
               "\r\n"
               "line1\r\nline2");
    // No bare LF anywhere.
    for (size_t i = 0; i < m.size(); i++)
        if (m[i] == '\n') REQUIRE(m[i - 1] == '\r');
}

TEST_CASE("rfc822 builder: unset headers are omitted")
{
    MailDraft d = parsed("TO: a@b\n\nbody\n");
    REQUIRE(mail_draft_finalize(d, nullptr) == MailDraftError::NONE);
    std::string m = mail_draft_rfc822(d, nullptr);
    CHECK(m.find("Cc:") == std::string::npos);
    CHECK(m.find("Bcc:") == std::string::npos);
    CHECK(m.find("Subject:") == std::string::npos);
    CHECK(m.find("In-Reply-To:") == std::string::npos);
    CHECK(m.find("References:") == std::string::npos);
}

TEST_CASE("rfc822 builder: reply threading headers")
{
    MailReplyTarget t;
    t.threadId = "t1";
    t.messageId = "<m3@x>";
    t.references = "<m1@x> <m2@x>";
    t.from = "alice@x";
    t.subject = "Topic";

    MailDraft d = parsed("\nThanks!\n");
    REQUIRE(mail_draft_finalize(d, &t) == MailDraftError::NONE);

    SUBCASE("References chains the target's Message-ID")
    {
        std::string m = mail_draft_rfc822(d, &t);
        CHECK(m.find("In-Reply-To: <m3@x>\r\n") != std::string::npos);
        CHECK(m.find("References: <m1@x> <m2@x> <m3@x>\r\n") != std::string::npos);
    }
    SUBCASE("no prior References -> a chain of one")
    {
        t.references.clear();
        std::string m = mail_draft_rfc822(d, &t);
        CHECK(m.find("References: <m3@x>\r\n") != std::string::npos);
    }
    SUBCASE("no Message-ID -> no threading headers at all")
    {
        t.messageId.clear();
        std::string m = mail_draft_rfc822(d, &t);
        CHECK(m.find("In-Reply-To:") == std::string::npos);
        CHECK(m.find("References:") == std::string::npos);
    }
}

TEST_CASE("rfc822 builder: UTF-8 passes through unencoded")
{
    MailDraft d = parsed("TO: a@b\nSUBJECT: caf\xc3\xa9\n\nna\xc3\xafve\n");
    REQUIRE(mail_draft_finalize(d, nullptr) == MailDraftError::NONE);
    std::string m = mail_draft_rfc822(d, nullptr);
    CHECK(m.find("Subject: caf\xc3\xa9\r\n") != std::string::npos);
    CHECK(m.find("na\xc3\xafve") != std::string::npos);
}
