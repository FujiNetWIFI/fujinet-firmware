// Link-time stand-ins for the ROM mount tests, built once per target against
// that target's systemBus. FujiBus packets are recorded in `bus`.

#include "romMountStubs.h"
#include "fujiHost.h"

systemBus SYSTEM_BUS;
BusLog bus;
HostFiles host_files;

void systemBus::transaction_accept(transState_t) {}
void systemBus::transaction_success() {}
void systemBus::transaction_error() {}
size_result_t systemBus::transaction_get_length(void *, size_t) { RETURN_SIZE_ERROR(); }
void systemBus::transaction_send(const void *, size_t, bool) {}

success_is_true systemBus::writeFujiBusPacket(FujiBusPacket &packet)
{
    Sent sent{packet.device(), packet.command(), {}, packet.dataAsString().value_or("")};
    for (unsigned int idx = 0; idx < packet.paramCount(); idx++)
        sent.params.push_back(packet.param(idx));
    bus.sent.push_back(sent);
    RETURN_SUCCESS_AS_TRUE();
}

std::unique_ptr<FujiBusPacket> systemBus::readFujiBusPacket(int)
{
    size_t idx = bus.sent.size() - 1;
    Reply reply = idx < bus.replies.size() ? bus.replies[idx] : Reply::Ack;
    if (reply == Reply::None)
        return nullptr;
    return std::make_unique<FujiBusPacket>(FUJI_DEVICEID::DBC,
                                           reply == Reply::Ack ? CMD::FUJI_ACK : CMD::FUJI_NAK);
}

// A host serving host_files, for the rs232 .cfg sibling lookup.
void fujiHost::set_type(fujiHostType type) { _type = type; }
const char *fujiHost::get_prefix() { return ""; }

bool fujiHost::file_exists(const char *path)
{
    return host_files.files.count(path) != 0;
}

fnFile *fujiHost::fnfile_open(const char *path, char *, int, const char *)
{
    auto found = host_files.files.find(path);
    if (found == host_files.files.end())
        return nullptr;
    host_files.opened.push_back(std::make_unique<MemFile>(found->second));
    return host_files.opened.back().get();
}

file_size_t fujiHost::file_size(fnFile *filehandle)
{
    long here = fnio::ftell(filehandle);
    fnio::fseek(filehandle, 0, SEEK_END);
    long size = fnio::ftell(filehandle);
    fnio::fseek(filehandle, here, SEEK_SET);
    return file_size_t(size);
}

BoIPChannel::BoIPChannel() : _ip(0), _port(0), _listening(false), _local_echo(false),
    _non_blocking(false), _no_delay(false), _fd(-1), _listen_fd(-1), _state(BoIPStopped) {}
BoIPChannel::~BoIPChannel() = default;
void BoIPChannel::updateFIFO() {}
size_t BoIPChannel::dataOut(const void *, size_t) { return 0; }
void BoIPChannel::end() {}
void BoIPChannel::flushOutput() {}
