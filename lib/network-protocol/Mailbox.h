/**
 * Base interface for Protocol adapters that read mailboxes.
 *
 * Analogous to NetworkProtocolFS, but for e-mail: it abstracts reading a
 * folder index, a message, an attachment index, or an attachment, and emits
 * human-readable (or, on request, raw binary) output for e-mail programs and
 * utilities running on the retro host.
 *
 * Concrete subclasses (GMAIL, IMAPS) supply only the provider data layer via
 * the pure-virtual hooks below; this base owns the OPEN/STATUS/READ/CLOSE
 * template and all shared formatting.
 *
 * Devicespec / operation model (path depth x access mode):
 *
 *   /FOLDER           mode 4 (READ)  -> folder message count ("<n><EOL>")
 *   /FOLDER           mode 6 (DIR)   -> message index (human or raw)
 *   /FOLDER/N         mode 4 (READ)  -> message body (primary text part)
 *   /FOLDER/N         mode 6 (DIR)   -> attachment index (human or raw)
 *   /FOLDER/N/A       mode 4 (READ)  -> attachment data (A=0 -> primary body)
 *   /                 mode 8 (WRITE) -> compose a new message
 *   /FOLDER/N         mode 8 (WRITE) -> reply to message N (target resolved at open)
 *
 * N is the message's sequence position within the folder.
 *
 * Write model: the host writes a message RFC822-style -- TO/CC/BCC/SUBJECT
 * header lines (case-insensitive keys; TO/CC/BCC append on repeat, SUBJECT is
 * last-wins; an unknown key or a colon-less header line is an error), then the
 * first blank line ends the headers and everything after it is body, verbatim.
 * Lines end with any mix of 0x9B/CR/LF/CRLF. CLOSE commits the send; closing
 * without writing aborts, and re-opening the unit without a close discards the
 * draft. A reply defaults TO (target's Reply-To, else From) and SUBJECT
 * ("Re: ...", never doubled) when the host omits them. Providers that cannot
 * send leave can_write() false and a WRITE open fails READ_ONLY.
 *
 * Query params on a folder index: range=START-END (absolute, inclusive,
 * 0-based; default first 20) and newest=1 (default, descending; 0 ascending).
 *
 * For DIR ops, aux2 (translate) selects the output format: 255 = raw binary
 * structs; any other value = human-readable, with the line width taken from the
 * low 7 bits (0 -> default). NOS opens a directory with aux2=128, which yields a
 * human-readable listing at the default width. For READ ops, aux2 is the real
 * netProtoTranslation_t EOL code.
 */

#ifndef NETWORKPROTOCOL_MAILBOX
#define NETWORKPROTOCOL_MAILBOX

#include "Protocol.h"
#include "mail_draft.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma pack(push, 1)
// Raw index entry emitted when a message index is opened with DIR + bit 7 set.
typedef struct _mailIndexItem
{
    uint32_t msgNum;
    char     displayName[32];
    char     emailAddress[48];
    char     subject[128];
    uint64_t timestamp;
} MailIndexItem;

// Raw attachment entry emitted when an attachment index is opened with bit 7 set.
typedef struct _mailAttachmentItem
{
    uint8_t  attachmentNum;
    char     displayName[128];
    char     fileName[128];
    char     mimeType[48];
    uint64_t length;
} MailAttachmentItem;
#pragma pack(pop)

static_assert(sizeof(MailIndexItem) == 220, "MailIndexItem wire layout changed");
static_assert(sizeof(MailAttachmentItem) == 313, "MailAttachmentItem wire layout changed");

// Structured index entry the provider hooks fill in (host-native types).
struct MailboxIndexEntry
{
    uint32_t    msgNum = 0;
    bool        important = false;
    std::string displayName;
    std::string emailAddress;
    std::string subject;
    uint64_t    timestamp = 0; // seconds since epoch
};

// Structured attachment entry the provider hooks fill in.
struct MailboxAttachmentEntry
{
    uint8_t     attachmentNum = 0;
    std::string displayName;
    std::string fileName;
    std::string mimeType;
    uint64_t    length = 0;
};

class NetworkProtocolMailbox : public NetworkProtocol
{
public:
    NetworkProtocolMailbox(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolMailbox();

    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                     netProtoTranslation_t translate) override;
    fujiError_t read(unsigned short len) override;
    fujiError_t write(unsigned short len) override;
    fujiError_t status(NetworkStatus *status) override;
    fujiError_t close() override;
    size_t      available() override;

    // Default human-readable line width, used when the width parameter (aux2
    // low 7 bits) is 0. Defaulted per platform in the ctor; settable here.
    void setDefaultHumanWidth(int w) { if (w > 0) _defaultWidth = w; }

protected:
    // ---- provider hooks (implemented by GMAIL / IMAPS) ----

    // Connect and authenticate. Called once at open, before any fetch.
    virtual fujiError_t connect_and_auth() = 0;

    // Number of messages in folder.
    virtual fujiError_t folder_count(const std::string &folder, uint32_t &count) = 0;

    // Messages at absolute positions [rangeStart, rangeEnd] (inclusive).
    // msgNum is the folder sequence position. newest=true -> descending.
    virtual fujiError_t folder_index(const std::string &folder, long rangeStart, long rangeEnd,
                                     bool newest, std::vector<MailboxIndexEntry> &out) = 0;

    // Primary text body of message seq (prefer text/plain, else text/html).
    virtual fujiError_t message_body(const std::string &folder, uint32_t seq, std::string &out) = 0;

    // Attachment list for message seq. Entry 0 is the primary body.
    virtual fujiError_t attachment_index(const std::string &folder, uint32_t seq,
                                         std::vector<MailboxAttachmentEntry> &out) = 0;

    // Attachment data. attach==0 -> primary body.
    virtual fujiError_t attachment_data(const std::string &folder, uint32_t seq, uint8_t attach,
                                        std::string &out) = 0;

    // Map the last provider error into `error` (nDevStatus_t).
    virtual void mailbox_error_to_error() = 0;

    // Whether this provider can send mail. Gates WRITE opens. (IMAPS: cannot.)
    virtual bool can_write() const { return false; }

    // Snapshot the reply target for /FOLDER/N at open. Default: read-only.
    virtual fujiError_t reply_target(const std::string &folder, uint32_t seq,
                                     MailReplyTarget &out);

    // Send a finalized draft. reply == nullptr for compose. Default: read-only.
    virtual fujiError_t message_send(const MailDraft &d, const MailReplyTarget *reply);

    // ---- parsed request state (populated by open) ----
    std::string _folder;
    uint32_t    _seq = 0;
    uint8_t     _attach = 0;

    // Default human-readable line width (set per platform in the ctor).
    int _defaultWidth = 40;

private:
    // Operation dispatch (combines path depth with access mode).
    fujiError_t do_folder_count();
    fujiError_t do_folder_index(uint8_t transByte);
    fujiError_t do_message_body();
    fujiError_t do_attachment_index(uint8_t transByte);
    fujiError_t do_attachment_data();

    // Shared formatting.
    void format_index_human(const std::vector<MailboxIndexEntry> &items, int width);
    void format_index_raw(const std::vector<MailboxIndexEntry> &items);
    void format_attachment_index_human(const std::vector<MailboxAttachmentEntry> &items, int width);
    void format_attachment_index_raw(const std::vector<MailboxAttachmentEntry> &items);

    static std::vector<std::string> split_path(const std::string &path);

    // ---- write channel (compose / reply) ----
    bool _writeMode = false;
    bool _isReply = false;      // reply target resolved at open
    bool _committed = false;    // commit runs once even if close() re-enters
    bool _writeFailed = false;  // an overflowed write must not commit truncated data
    MailReplyTarget _replyTarget;
    std::string _writeBuf;

    fujiError_t commit_write();
};

#endif /* NETWORKPROTOCOL_MAILBOX */
