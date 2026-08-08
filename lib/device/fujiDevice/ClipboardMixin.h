#ifndef CLIPBOARDMIXIN_H
#define CLIPBOARDMIXIN_H

#include "FujiDeviceMixin.h"

#ifdef FUJI_MIXINS_ENABLED
#define FUJI_CLIPBOARD_MIXIN_ENABLED

#include "../../clipboard/clipboardManager.h"

/**
 * Clipboard commands.
 *
 * Snippets are addressed by index: 0 is the clipboard itself, 1..n are the
 * remembered snippets, newest first.
 *
 *   CLIPBOARD_STATUS (index)  select a snippet for reading and report on it,
 *                             see ClipboardStatus below
 *   CLIPBOARD_OUTPUT (len)    send the next len bytes of the selected snippet
 *   CLIPBOARD_INPUT (len)     append len bytes to the staged write, repeat as
 *                             needed, then commit with CLIPBOARD_CONTROL
 *   CLIPBOARD_CONTROL (op, arg) everything that changes state, see the ops below
 *
 * So writing is INPUT... then CONTROL(COMMIT), and reading is STATUS then
 * OUTPUT... until the reported length has been consumed.
 */

enum clipboardControlOp_t : uint8_t
{
    // Make the staged write the clipboard. arg bit 0 set means the content is
    // binary and must not be translated.
    CLIPBOARD_CTRL_COMMIT      = 0,
    // Empty the clipboard. arg 0 keeps the remembered snippets, 1 forgets them.
    CLIPBOARD_CTRL_CLEAR       = 1,
    // Set the end of line translation mode. arg is the mode, using the same
    // values as the Network device (0 none, 1 CR, 2 LF, 3 CR/LF, 4 PETSCII).
    CLIPBOARD_CTRL_TRANSLATION = 2,
};

// Flags in ClipboardStatus::flags
enum clipboardStatusFlags_t : uint8_t
{
    CLIPBOARD_FLAG_PRESENT = 0x01, // the requested snippet exists
    CLIPBOARD_FLAG_BINARY  = 0x02, // it holds binary content
};

struct ClipboardStatus
{
    u32ne_t length;      // bytes the selected snippet will deliver via OUTPUT
    uint8_t count;       // remembered snippets, not counting the clipboard
    uint8_t translation; // current translation mode
    uint8_t flags;       // clipboardStatusFlags_t
} __attribute__((packed));

class ClipboardMixin : public FujiDeviceMixin
{
private:
    FujiMixinCommandHandlers handlers;

protected:
    FujiMixinCommandHandlers commandHandlers() override { return handlers; }

    void clipboard_input(const FUJI_COMMAND_PACKET &packet);
    void clipboard_output(const FUJI_COMMAND_PACKET &packet);
    void clipboard_status(const FUJI_COMMAND_PACKET &packet);
    void clipboard_control(const FUJI_COMMAND_PACKET &packet);

public:
    ClipboardMixin() {
        handlers = {
            { FUJICMD_CLIPBOARD_INPUT,   FM_CMD_HANDLER(clipboard_input)   },
            { FUJICMD_CLIPBOARD_OUTPUT,  FM_CMD_HANDLER(clipboard_output)  },
            { FUJICMD_CLIPBOARD_STATUS,  FM_CMD_HANDLER(clipboard_status)  },
            { FUJICMD_CLIPBOARD_CONTROL, FM_CMD_HANDLER(clipboard_control) },
        };
    }
};

#endif // FUJI_MIXINS_ENABLED

#endif /* CLIPBOARDMIXIN_H */
