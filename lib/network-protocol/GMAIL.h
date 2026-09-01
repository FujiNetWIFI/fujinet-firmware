#ifndef NETWORKPROTOCOLGMAIL_H
#define NETWORKPROTOCOLGMAIL_H

#include "Mailbox.h"

#include <cJSON.h>
#include <string>
#include <vector>

/**
 * NetworkProtocolGMAIL
 *
 * Gmail mailbox adapter for FujiNet.
 *
 * URL format:  GMAIL:///FOLDER            (message count / index)
 *              GMAIL:///FOLDER/N          (message body / attachment index)
 *              GMAIL:///FOLDER/N/A        (attachment data; A=0 -> body)
 *              GMAIL:///                  (mode 8: compose; see Mailbox.h)
 *              GMAIL:///FOLDER/N          (mode 8: reply to message N)
 *
 * Authentication reuses the existing Google Drive OAuth grant stored in
 * fnConfig [GoogleDrive]; the grant's scope must include gmail.readonly, plus
 * gmail.send to compose or reply. A grant issued before a scope was added
 * never gains it -- re-authorize Google in the web UI.
 * Tokens are refreshed automatically via the relay, exactly like GDRIVE.
 *
 * A reply threads via the target's threadId and Message-ID; note Gmail only
 * groups it visually when the subject still matches, so a host-overridden
 * SUBJECT may appear outside the conversation.
 *
 * A folder maps to a Gmail label (case-insensitive, e.g. "Inbox" -> INBOX).
 * The message sequence number N is the message's position within the folder:
 * newest message == messagesTotal, oldest == 1.
 */
class NetworkProtocolGMAIL : public NetworkProtocolMailbox
{
public:
    NetworkProtocolGMAIL(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolGMAIL();

    NetworkProtocolGMAIL(const NetworkProtocolGMAIL &) = delete;
    NetworkProtocolGMAIL &operator=(const NetworkProtocolGMAIL &) = delete;

protected:
    fujiError_t connect_and_auth() override;
    fujiError_t folder_count(const std::string &folder, uint32_t &count) override;
    fujiError_t folder_index(const std::string &folder, long rangeStart, long rangeEnd,
                             bool newest, std::vector<MailboxIndexEntry> &out) override;
    fujiError_t message_body(const std::string &folder, uint32_t seq, std::string &out) override;
    fujiError_t attachment_index(const std::string &folder, uint32_t seq,
                                 std::vector<MailboxAttachmentEntry> &out) override;
    fujiError_t attachment_data(const std::string &folder, uint32_t seq, uint8_t attach,
                                std::string &out) override;
    void mailbox_error_to_error() override;

    bool can_write() const override { return true; }
    fujiError_t reply_target(const std::string &folder, uint32_t seq,
                             MailReplyTarget &out) override;
    fujiError_t message_send(const MailDraft &d, const MailReplyTarget *reply) override;

private:
    std::string _access_token;
    int _last_http = 0; // last HTTP status, for error mapping

    // ---- OAuth (reuses GDRIVE token storage / relay) ----
    bool ensure_access_token();
    bool refresh_access_token();

    // ---- HTTP helpers (mirror GDRIVE) ----
    std::string api_get(const std::string &url);
    std::string api_post(const std::string &url, const std::string &body,
                         const std::string &content_type);
    static std::string url_encode(const std::string &s);
    static std::string json_str(cJSON *obj, const char *key);

    // ---- Gmail helpers ----
    // Resolve a folder name to a label id and (optionally) its message total.
    std::string label_id_for(const std::string &folder, uint32_t *total);
    // Newest-first message ids for a label, up to `needed` entries.
    std::vector<std::string> list_message_ids(const std::string &labelId, size_t needed);
    // Resolve a folder sequence number to a Gmail message id. ok=false on range error.
    std::string message_id_for_seq(const std::string &folder, uint32_t seq, bool &ok);
    // Fetch full message JSON (format=full). Caller owns/deletes the cJSON.
    cJSON *get_message_full(const std::string &id);
    // Decode a text body part (prefer text/plain, then text/html) from a payload.
    bool extract_body(cJSON *payload, std::string &out);
    // Collect attachment parts (parts carrying a filename) from a payload.
    void collect_attachments(cJSON *payload, std::vector<cJSON *> &out);

    static constexpr const char *GMAIL_BASE = "https://gmail.googleapis.com/gmail/v1/users/me";
};

#endif /* NETWORKPROTOCOLGMAIL_H */
