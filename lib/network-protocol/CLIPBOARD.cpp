/**
 * CLIPBOARD: protocol implementation
 */

#include "CLIPBOARD.h"

#include <algorithm>
#include <cstdlib>

#include "clipboardManager.h"

#include "../../include/debug.h"
#include "status_error_codes.h"

/**
 * Clipboard text is stored with LF line endings, so LF is the network side of
 * every translation, whichever EOL mode the channel was opened with. Mode 0
 * hands the snippet over exactly as it is stored.
 */
static void translate_to_computer(std::string &buf, netProtoTranslation_t mode,
                                  const std::string &native_eol)
{
    if (mode == NETPROTO_TRANS_NONE)
        return;

    if (mode == NETPROTO_TRANS_PETSCII)
        netproto_translate_to_computer(buf, mode, native_eol);

    netproto_translate_to_computer(buf, NETPROTO_TRANS_LF, native_eol);
}

/**
 * The inverse of translate_to_computer(): fold what the computer sent back to
 * the LF terminated form the clipboard stores.
 */
static void translate_from_computer(std::string &buf, netProtoTranslation_t mode,
                                    const std::string &native_eol)
{
    if (mode == NETPROTO_TRANS_NONE)
        return;

    netproto_translate_from_computer(buf, NETPROTO_TRANS_LF, native_eol);

    if (mode == NETPROTO_TRANS_PETSCII)
        netproto_translate_from_computer(buf, mode, native_eol);
}

NetworkProtocolClipboard::NetworkProtocolClipboard(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    name = "CLIPBOARD";
}

NetworkProtocolClipboard::~NetworkProtocolClipboard()
{
    readBuffer.clear();
    writeBuffer.clear();
}

bool NetworkProtocolClipboard::parse_index(PeoplesUrlParser *urlParser, size_t &idx)
{
    // CLIPBOARD:///0 leaves the index in the path, CLIPBOARD://0 leaves it in
    // the host. Accept either, and nothing at all as the clipboard itself.
    std::string spec = urlParser->path;

    if (!spec.empty() && spec.front() == '/')
        spec.erase(0, 1);

    if (spec.empty())
        spec = urlParser->host;

    if (spec.empty())
    {
        idx = 0;
        return true;
    }

    if (spec.find_first_not_of("0123456789") != std::string::npos)
        return false;

    idx = strtoul(spec.c_str(), nullptr, 10);

    return idx < CLIPBOARD_MAX_SNIPPETS;
}

fujiError_t NetworkProtocolClipboard::open(PeoplesUrlParser *urlParser,
                                           fileAccessMode_t access,
                                           netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);

    if (!parse_index(urlParser, index))
    {
        Debug_printf("NetworkProtocolClipboard::open() - No such snippet: %s\r\n", urlParser->path.c_str());
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return FUJI_ERROR::UNSPECIFIED;
    }

    switch (access)
    {
    case ACCESS_MODE::READ:
        readable = true;
        break;
    case ACCESS_MODE::WRITE:
        writable = truncates = true;
        break;
    case ACCESS_MODE::APPEND:
        writable = appends = true;
        break;
    case ACCESS_MODE::READWRITE:
        readable = writable = true;
        break;
    default:
        Debug_printf("NetworkProtocolClipboard::open() - Unsupported access mode: %u\r\n", (unsigned)access);
        error = NDEV_STATUS::INVALID_COMMAND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Only the clipboard itself can be replaced; the remembered snippets are
    // what it used to hold, and are read only.
    if (writable && index != 0)
    {
        Debug_printf("NetworkProtocolClipboard::open() - Snippet %u is read only\r\n", (unsigned)index);
        error = NDEV_STATUS::ACCESS_DENIED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Index 0 always exists, even when the clipboard is empty; a remembered
    // snippet that was never filled does not.
    if (fnClipboard.snippet(index) == nullptr)
    {
        Debug_printf("NetworkProtocolClipboard::open() - Snippet %u is not there\r\n", (unsigned)index);
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    binary = urlParser->queryParam("binary") == "1";

    if (readable)
        stage_read();

    Debug_printf("NetworkProtocolClipboard::open(%u) - %u bytes to read, mode %u%s\r\n",
                 (unsigned)index, (unsigned)readBuffer.size(), (unsigned)translation_mode,
                 binary ? ", binary" : "");

    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

void NetworkProtocolClipboard::stage_read()
{
    const ClipboardSnippet *snippet = fnClipboard.snippet(index);
    if (snippet == nullptr)
        return;

    readBuffer = snippet->data;

    // A snippet marked binary is handed over byte for byte, whatever the
    // channel asked for.
    if (!snippet->binary)
        translate_to_computer(readBuffer, translation_mode, native_eol);
}

void NetworkProtocolClipboard::commit()
{
    std::string data = std::move(writeBuffer);
    writeBuffer.clear();

    // Translation happens here rather than in write(), so a native EOL that
    // straddles two writes is still recognized.
    if (!binary)
        translate_from_computer(data, translation_mode, native_eol);

    if (appends)
    {
        const ClipboardSnippet *current = fnClipboard.snippet(0);
        if (current != nullptr)
            data.insert(0, current->data);
    }

    Debug_printf("NetworkProtocolClipboard::commit() - %u bytes%s\r\n",
                 (unsigned)data.size(), binary ? ", binary" : "");

    fnClipboard.set(std::move(data), CLIPBOARD_SOURCE_COMPUTER, binary);

    // Nothing is owed to the clipboard any more, so a second close() is a
    // no-op rather than a second, empty snippet.
    wrote = truncates = false;
}

fujiError_t NetworkProtocolClipboard::close()
{
    if (writable && !transmitBuffer->empty())
        write(transmitBuffer->length());

    // A write only open replaces the clipboard even if nothing arrived, which
    // is how it is emptied from the computer.
    if (writable && (wrote || truncates))
        commit();

    readBuffer.clear();
    readBuffer.shrink_to_fit();
    writeBuffer.clear();
    writeBuffer.shrink_to_fit();

    return NetworkProtocol::close();
}

fujiError_t NetworkProtocolClipboard::read(unsigned short len)
{
    was_write = false;

    if (!readable)
    {
        error = NDEV_STATUS::WRITE_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (receiveBuffer->length() < len)
    {
        size_t take = std::min((size_t)(len - receiveBuffer->length()), readBuffer.size());

        receiveBuffer->append(readBuffer, 0, take);
        readBuffer.erase(0, take);
        readBuffer.shrink_to_fit();
    }

    // readBuffer was translated when the snippet was staged, so what lands in
    // the receive buffer is already in the computer's representation.
    if (receiveBuffer->length() < len)
    {
        // Short: pad out the block the channel is expecting and say why.
        receiveBuffer->append(len - receiveBuffer->length(), '\0');
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolClipboard::write(unsigned short len)
{
    was_write = true;

    if (!writable)
    {
        error = NDEV_STATUS::READ_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (len > transmitBuffer->length())
        len = transmitBuffer->length();

    if (writeBuffer.size() + len > CLIPBOARD_MAX_SIZE)
    {
        Debug_printf("NetworkProtocolClipboard::write(%u) - Would exceed the %u byte limit\r\n",
                     len, (unsigned)CLIPBOARD_MAX_SIZE);
        transmitBuffer->erase(0, len);
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    writeBuffer.append(*transmitBuffer, 0, len);
    transmitBuffer->erase(0, len);
    transmitBuffer->shrink_to_fit();

    wrote = true;
    error = NDEV_STATUS::SUCCESS;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolClipboard::status(NetworkStatus *status)
{
    size_t remaining = available();

    status->connected = remaining > 0 ? 1 : 0;

    if (was_write || !readable)
        status->error = NDEV_STATUS::SUCCESS;
    else
        status->error = remaining > 0 ? error : NDEV_STATUS::END_OF_FILE;

    return FUJI_ERROR::NONE;
}

size_t NetworkProtocolClipboard::available()
{
    return readBuffer.size() + receiveBuffer->length();
}
