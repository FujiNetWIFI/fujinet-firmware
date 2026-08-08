#include "ClipboardMixin.h"

#ifdef FUJI_CLIPBOARD_MIXIN_ENABLED

#include "debug.h"

void ClipboardMixin::clipboard_input(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    Debug_printf("ClipboardMixin: INPUT (len: %d)\n", len);

    if (!len)
    {
        Debug_printf("Invalid length. Aborting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    std::string p(len, 0);
    SYSTEM_BUS.transaction_get(p.data(), len);

    if (!fnClipboard.write_append(p.data(), p.size()))
    {
        // Whatever was staged is unusable now, so don't leave it lying around.
        fnClipboard.write_abort();
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

void ClipboardMixin::clipboard_output(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t len = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("ClipboardMixin: OUTPUT (len: %d)\n", len);

    if (!len)
    {
        Debug_printf("Refusing to send a zero byte buffer. Aborting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }
    else if (len > fnClipboard.read_available())
    {
        Debug_printf("Requested %u bytes, but only %u are staged, aborting.\n",
                     len, (unsigned)fnClipboard.read_available());
        SYSTEM_BUS.transaction_error();
        return;
    }

    std::vector<unsigned char> p(len);
    fnClipboard.read_take(p.data(), len);

    SYSTEM_BUS.transaction_send(p.data(), len, false);
}

void ClipboardMixin::clipboard_status(const FUJI_COMMAND_PACKET &packet)
{
    uint8_t index = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("ClipboardMixin: STATUS (snippet: %d)\n", index);

    ClipboardStatus status = {};
    status.count = fnClipboard.history_count();
    status.translation = fnClipboard.translation();

    // Selecting a snippet that isn't there is not an error: a caller that only
    // wants the count still gets a usable answer.
    const ClipboardSnippet *snippet = fnClipboard.snippet(index);
    if (snippet != nullptr)
    {
        std::string data = fnClipboard.snapshot_for_computer(index);
        if (!snippet->binary)
            data = SYSTEM_BUS.unicodeTextToNative(data);

        fnClipboard.read_stage(std::move(data));

        status.length = fnClipboard.read_available();
        status.flags |= CLIPBOARD_FLAG_PRESENT;
        if (snippet->binary)
            status.flags |= CLIPBOARD_FLAG_BINARY;
    }
    else
    {
        fnClipboard.read_stage(std::string());
    }

    Debug_printf("Clipboard snippet is %u bytes, %u remembered\n",
                 (size_t) status.length, status.count);

    SYSTEM_BUS.transaction_send(&status, sizeof(status), false);
}

void ClipboardMixin::clipboard_control(const FUJI_COMMAND_PACKET &packet)
{
    uint8_t op = packet.param(0);
    uint8_t arg = packet.param(1);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("ClipboardMixin: CONTROL (op: %d, arg: %d)\n", op, arg);

    switch (op)
    {
    case CLIPBOARD_CTRL_COMMIT:
    {
        bool binary = arg & 0x01;

        Debug_printf("Committing %u bytes%s\n",
                     (unsigned)fnClipboard.write_pending(), binary ? ", binary" : "");

        std::string data = fnClipboard.write_take();
        if (!binary)
            data = SYSTEM_BUS.nativeTextToUnicode(data);

        fnClipboard.set_from_computer(std::move(data), binary);
        break;
    }

    case CLIPBOARD_CTRL_CLEAR:
        if (arg)
            fnClipboard.clear_all();
        else
            fnClipboard.clear();
        break;

    case CLIPBOARD_CTRL_TRANSLATION:
        if (arg > NETPROTO_TRANS_PETSCII)
        {
            Debug_printf("Unknown translation mode. Aborting\n");
            SYSTEM_BUS.transaction_error();
            return;
        }
        fnClipboard.set_translation((netProtoTranslation_t) arg);
        break;

    default:
        Debug_printf("Unknown control op. Aborting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

#endif // FUJI_CLIPBOARD_MIXIN_ENABLED
