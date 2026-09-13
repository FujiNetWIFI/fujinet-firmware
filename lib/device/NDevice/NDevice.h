#ifndef NDEVICE_H
#define NDEVICE_H

#include "bus.h"
#include "Protocol.h"
#include "FS.h"

#include <unordered_map>

#define SPECIAL_BUFFER_SIZE 256

typedef struct {
    u16ne_t avail;
    uint8_t conn;
    nDevStatus_t err;
} NDeviceStatus;
static_assert(sizeof(NDeviceStatus) == 4, "NDeviceStatus must be 4 bytes");

typedef enum class PARSER : uint8_t {
    NONE = 0,
    JSON = 1,
    SGML = 2,
} parserMode_t;

typedef enum class PARSER_PARAM : uint8_t {
    QUERY = 0,
    EOL = 1,
} parserParam_t;

// Reads the param using Enum's underlying integer type (so packet.param()
// knows the correct size), then casts that integer to the enum itself.
// Requires Enum to have an explicit underlying type, e.g. `enum class Foo : uint8_t`.
template <typename Enum>
Enum param_cast(const FUJI_COMMAND_PACKET &pkt, std::size_t idx) {
    using U = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<U>(pkt.param(idx)));
}

// Reads a packet param using the requested integer size.
template <typename T>
T param_as(const FUJI_COMMAND_PACKET &pkt, std::size_t idx) {
    return static_cast<T>(pkt.param(idx));
}

class NParser;

class NDevice : public virtualDevice
{
public:
    NDevice();
    virtual ~NDevice();

    /**
     * The one bus-facing entry point. Looks up packet.command() and calls
     * the handler. Returns true if the command was recognized (regardless of
     * whether the handler itself then signaled a bus error), false if it
     * wasn't.
     */
    bool processCommand(const FUJI_COMMAND_PACKET &packet);

    // Return true if command is one that can be handled
    bool recognizesCommand(fujiCommandID_t command);

    /** returns true when data is available */
    bool poll_interrupt();

protected:
    /**
     * Timer Rate for interrupt timer (ms)
     */
#ifdef ESP_PLATFORM
    uint8_t timerRate = 100;
#else
    uint8_t timerRate = 20;
#endif

    std::unique_ptr<NParser> _parser;

    // FIMXME - NetworkProtocol should manage its own buffers
    std::string *receiveBuffer = nullptr;
    std::string *transmitBuffer = nullptr;
    std::string *specialBuffer = nullptr;
    std::unique_ptr<PeoplesUrlParser> urlParser;

    /** Instance of the currently open network protocol, if any. */
    std::unique_ptr<NetworkProtocol> _protocol = nullptr;

    uint64_t readAck = 0;

#ifdef HAVE_LAST_ERROR
    /**
     * The last operation's error code, remembered so a later bare STATUS
     * command (no open protocol) can report it.
     */
    nDevStatus_t lastError = NDEV_STATUS::SUCCESS;
#endif /* HAVE_LAST_ERROR */

    /** Currently set prefix (CWD) for this N: device. */
    std::string prefix;

    /** Login/password for the current protocol action. */
    std::string login;
    std::string password;

    /**
     * Client-set override for the native EOL bytes a protocol adapter
     * translates to/from (NETCMD_SET_EOL). Empty means "use this platform's
     * default."
     */
    std::string network_eol_override;

    /**
     * Translation override ORed into trans_mode for READ/WRITE/STATUS operations.
     *   0 = No Translation
     *   1 = CR<->EOL (Macintosh)
     *   2 = LF<->EOL (UNIX)
     *   3 = CR/LF<->EOL (PC/Windows)
     */
    netProtoTranslation_t trans_override = NETPROTO_TRANS::NONE;

    // ---- hooks: override only where the hardware genuinely differs -------
    /** The line ending network_eol_override resolves to when unset. */
    std::string network_eol() const;

    /** Column width for LONG-format directory listings, or 0 to skip
        calling protocol->setDirLongWidth() at all. */
    virtual int dir_long_width() const { return 0; }

#ifdef HAVE_LAST_ERROR
    /**
     * Populate a STATUS reply when no protocol is bound. Default just
     * reports lastError. SIO/RS232/DriveWire override to also serve
     * IP/netmask/gateway/DNS queries via the mode byte.
     */
    virtual NDeviceStatus status_local(uint8_t mode);
#endif /* HAVE_LAST_ERROR */

    void fujidev_open(const FUJI_COMMAND_PACKET &packet);
    void fujidev_close(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_read(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_write(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_status(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_set_prefix(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_get_prefix(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_set_query(const FUJI_COMMAND_PACKET &packet);

    error_is_true fujicore_write(const ByteBuffer &buf);
    error_is_true fujicore_read(ByteBuffer &buf, size_t len);
    size_t fujicore_available();
    NDeviceStatus fujicore_status();
    error_is_true fujicore_set_query(const std::string &query, uint8_t parseFlags);
    error_is_true fujicore_set_prefix(const std::string &pathSpec);
    error_is_true fujicore_seek(size_t offset);

    /**
     * Parse a devicespec into a URL and instantiate the matching protocol.
     * On success, `protocol` is set and true is returned, with url_out
     * holding the parsed URL (borrow it -- don't let it outlive protocol).
     * On failure, `protocol` is left null, lastError/the bus error have
     * already been signaled, and false is returned.
     */
    bool parse_and_instantiate_protocol(std::string &deviceSpec, bool is_dir,
                                        std::unique_ptr<PeoplesUrlParser> &url_out);

    void fujidev_set_login(const FUJI_COMMAND_PACKET &packet);
    void fujidev_set_password(const FUJI_COMMAND_PACKET &packet);
    void fujidev_set_parser(const FUJI_COMMAND_PACKET &packet);
    void fujidev_do_parse(const FUJI_COMMAND_PACKET &packet);
    void fujidev_set_parameter(const FUJI_COMMAND_PACKET &packet);
    void fujidev_set_eol(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_seek(const FUJI_COMMAND_PACKET &packet);
    virtual void fujidev_tell(const FUJI_COMMAND_PACKET &packet);

    fujiError_t read_channel(unsigned short num_bytes);
    fujiError_t write_channel(unsigned short num_bytes);

    // fs ops -- each is its own dispatch-table entry; fs_op() below is the shared plumbing.
    void fujidev_rename(const FUJI_COMMAND_PACKET &packet);
    void fujidev_delete(const FUJI_COMMAND_PACKET &packet);
    void fujidev_lock(const FUJI_COMMAND_PACKET &packet);
    void fujidev_unlock(const FUJI_COMMAND_PACKET &packet);
    void fujidev_mkdir(const FUJI_COMMAND_PACKET &packet);
    void fujidev_rmdir(const FUJI_COMMAND_PACKET &packet);

    // tcp ops
    void fujidev_tcp_accept(const FUJI_COMMAND_PACKET &packet);
    void fujidev_tcp_close_client(const FUJI_COMMAND_PACKET &packet);

    // http ops
    void fujidev_http_set_channel_mode(const FUJI_COMMAND_PACKET &packet);

    // udp ops
    void fujidev_udp_get_remote(const FUJI_COMMAND_PACKET &packet);
    void fujidev_udp_set_destination(const FUJI_COMMAND_PACKET &packet);

    void fujidev_set_translation(const FUJI_COMMAND_PACKET &packet) {
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        trans_override = (netProtoTranslation_t) static_cast<uint8_t>(packet.param(1));
        SYSTEM_BUS.transaction_success();
    }
    void fujidev_set_timer_rate(const FUJI_COMMAND_PACKET &packet);

private:
    using Handler = void (NDevice::*)(const FUJI_COMMAND_PACKET &);
    static const std::unordered_map<fujiCommandID_t, Handler> dispatch_table;

    /**
     * Shared plumbing for the six fs ops: WILL_GET-accept, parse the
     * devicespec into a one-shot protocol, dynamic_cast to NetworkProtocolFS,
     * call `op` on it, tear the one-shot protocol back down, and signal
     * success/error. Each of fs_rename/fs_delete/... is a one-line call
     * to this with a different member-function pointer for `op`.
     */
    void fs_op(const FUJI_COMMAND_PACKET &packet, fujiError_t (NetworkProtocolFS::*op)(PeoplesUrlParser *));

    NDeviceStatus current_status();
};

#endif /* NDEVICE_H */
