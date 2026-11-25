// license:BSD-3-Clause
// See trs80m1_fujinet.h.
#include "emu.h"
#include "trs80m1_fujinet.h"

#include "m1_card.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

DEFINE_DEVICE_TYPE(TRS80M1_FUJINET, trs80m1_fujinet_device, "trs80m1_fujinet",
                   "FujiNet TRS-80 Model I card")

namespace {

constexpr u32 SERVICE_US = 100;
constexpr int RECONNECT_MS = 1000;
constexpr int SYNC_WAIT_MS = 5000; // the longest one stall may last

} // namespace

struct trs80m1_fujinet_device::state
{
    m1_card_t card;
    int sock = -1;
    std::chrono::steady_clock::time_point next_connect{};
    bool sync = true;
    bool trace = false;
    bool power_config = false;
    std::string host = "127.0.0.1";
    std::string port = "1985";
};

static INPUT_PORTS_START(trs80m1_fujinet)
    PORT_START("SWITCHES")
    PORT_CONFNAME(0x01, 0x01, "FujiNet 32K RAM")
    PORT_CONFSETTING(0x00, DEF_STR(Off))
    PORT_CONFSETTING(0x01, DEF_STR(On))
    PORT_CONFNAME(0x02, 0x02, "FujiNet Expansion Interface")
    PORT_CONFSETTING(0x00, DEF_STR(Off))
    PORT_CONFSETTING(0x02, DEF_STR(On))
    PORT_CONFNAME(0x04, 0x04, "FujiNet RS-232")
    PORT_CONFSETTING(0x00, DEF_STR(Off))
    PORT_CONFSETTING(0x04, DEF_STR(On))
INPUT_PORTS_END

trs80m1_fujinet_device::trs80m1_fujinet_device(const machine_config &mconfig, const char *tag,
                                               device_t *owner, u32 clock)
    : device_t(mconfig, TRS80M1_FUJINET, tag, owner, clock),
      device_nvram_interface(mconfig, *this), m_int_cb(*this),
      m_switches(*this, "SWITCHES"), m_timer(nullptr), m_state(std::make_unique<state>())
{
}

trs80m1_fujinet_device::~trs80m1_fujinet_device() = default;

ioport_constructor trs80m1_fujinet_device::device_input_ports() const
{
    return INPUT_PORTS_NAME(trs80m1_fujinet);
}

u32 trs80m1_fujinet_device::now_us()
{
    return u32(machine().time().as_ticks(1000000));
}

// A switch set Off forces its block off.
u8 trs80m1_fujinet_device::forced_off()
{
    return u8(~m_switches->read() & M1_SET_ALL);
}

void trs80m1_fujinet_device::nvram_default()
{
    m_state->card.saved = M1_SET_ALL;
    m_state->card.settings.pipe_base = M1_PIPE_BASE;
}

bool trs80m1_fujinet_device::nvram_read(util::read_stream &file)
{
    u8 saved[2];
    auto const [err, actual] = util::read(file, saved, sizeof(saved));
    if (err || actual != sizeof(saved))
        return false;
    m_state->card.saved = u8(saved[0] & M1_SET_ALL);
    m_state->card.settings.pipe_base = saved[1];
    return true;
}

bool trs80m1_fujinet_device::nvram_write(util::write_stream &file)
{
    u8 saved[2] = {m_state->card.saved, m_state->card.settings.pipe_base};
    auto const [err, actual] = util::write(file, saved, sizeof(saved));
    return !err;
}

static void trace_link(const char *event, uint32_t arg1, uint32_t arg2)
{
    std::fprintf(stderr, "fujinet: %s %02X %04X\n", event, unsigned(arg1), unsigned(arg2));
}

void trs80m1_fujinet_device::device_start()
{
    state &conn = *m_state;
    if (const char *tcp = std::getenv("FUJINET_TCP"))
    {
        std::string hostport = tcp;
        size_t colon = hostport.rfind(':');
        if (colon != std::string::npos)
        {
            conn.host = hostport.substr(0, colon);
            conn.port = hostport.substr(colon + 1);
        }
    }
    const char *async = std::getenv("FUJINET_ASYNC");
    conn.sync = !(async && *async == '1');
    const char *trace = std::getenv("FUJINET_TRACE");
    conn.trace = trace && *trace == '1';
    const char *power = std::getenv("FUJINET_POWER_CONFIG");
    conn.power_config = power && *power == '1';

    m1_settings_t settings = {M1_SET_ALL, M1_PIPE_BASE};
    m1_card_init(&conn.card, &settings, 0, link_send, this, now_us());
    if (conn.trace)
        conn.card.link.trace = trace_link;
    m_timer = timer_alloc(FUNC(trs80m1_fujinet_device::service), this);
    m_timer->adjust(attotime::from_usec(SERVICE_US), 0, attotime::from_usec(SERVICE_US));
}

void trs80m1_fujinet_device::device_reset()
{
    // A power-on: the RAM starts blank rather than holding the last session.
    state &conn = *m_state;
    conn.card.forced_off = forced_off();
    std::memset(conn.card.ram, 0, sizeof(conn.card.ram));
    m1_bus_reset(&conn.card);
    if (conn.power_config)
        m1_card_power_on(&conn.card);
    update_int();
}

void trs80m1_fujinet_device::device_stop()
{
    if (m_state->sock >= 0)
        ::close(m_state->sock);
    m_state->sock = -1;
}

void trs80m1_fujinet_device::update_int()
{
    m_int_cb(m1_int_pending(&m_state->card) ? ASSERT_LINE : CLEAR_LINE);
}

/* ---- the bus ---- */

u8 trs80m1_fujinet_device::ei_r(offs_t offset)
{
    m1_card_t *card = &m_state->card;
    u16 addr = u16(M1_EI_BASE + offset);
    u8 val = 0xFF;
    m1_bus_idle(card);
    if (!m1_mem_peek(card, addr, now_us(), &val))
        return 0xFF;
    if (!machine().side_effects_disabled())
    {
        m1_mem_after_read(card, addr);
        update_int();
    }
    return val;
}

void trs80m1_fujinet_device::ei_w(offs_t offset, u8 data)
{
    m1_card_t *card = &m_state->card;
    m1_bus_idle(card);
    m1_mem_write(card, u16(M1_EI_BASE + offset), data, now_us());
    update_int();
}

u8 trs80m1_fujinet_device::ram_r(offs_t offset)
{
    u8 val = 0xFF;
    if (!m1_mem_peek(&m_state->card, u16(M1_RAM_BASE + offset), 0, &val))
        return 0xFF;
    return val;
}

void trs80m1_fujinet_device::ram_w(offs_t offset, u8 data)
{
    m1_mem_write(&m_state->card, u16(M1_RAM_BASE + offset), data, 0);
}

u8 trs80m1_fujinet_device::io_r(offs_t offset)
{
    m1_card_t *card = &m_state->card;
    u8 port = u8(offset);
    u8 val = 0xFF;
    m1_bus_idle(card);
    if (!m1_io_peek(card, port, now_us(), &val))
        return 0xFF;
    if (!machine().side_effects_disabled())
        m1_io_after_read(card, port);
    return val;
}

void trs80m1_fujinet_device::io_w(offs_t offset, u8 data)
{
    m1_io_write(&m_state->card, u8(offset), data);
}

void trs80m1_fujinet_device::sysres_w(int state)
{
    if (state)
    {
        m1_bus_reset(&m_state->card);
        update_int();
    }
}

/* ---- core0 and the link ---- */

size_t trs80m1_fujinet_device::link_send(void *ctx, const u8 *buf, size_t len)
{
    auto *dev = static_cast<trs80m1_fujinet_device *>(ctx);
    state &conn = *dev->m_state;
    size_t off = 0;
    while (off < len && conn.sock >= 0)
    {
        ssize_t sent = ::send(conn.sock, buf + off, len - off, MSG_NOSIGNAL);
        if (sent > 0)
            off += size_t(sent);
        else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            pollfd pfd = {conn.sock, POLLOUT, 0};
            ::poll(&pfd, 1, 100);
        }
        else
        {
            ::close(conn.sock);
            conn.sock = -1;
        }
    }
    return off;
}

void trs80m1_fujinet_device::link_connect()
{
    state &conn = *m_state;
    auto wall = std::chrono::steady_clock::now();
    if (conn.sock >= 0 || wall < conn.next_connect)
        return;
    conn.next_connect = wall + std::chrono::milliseconds(RECONNECT_MS);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (::getaddrinfo(conn.host.c_str(), conn.port.c_str(), &hints, &found) != 0)
        return;
    for (addrinfo *addr = found; addr; addr = addr->ai_next)
    {
        int sock = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sock < 0)
            continue;
        if (::connect(sock, addr->ai_addr, addr->ai_addrlen) == 0)
        {
            int one = 1;
            ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            ::fcntl(sock, F_SETFL, ::fcntl(sock, F_GETFL) | O_NONBLOCK);
            conn.sock = sock;
            break;
        }
        ::close(sock);
    }
    ::freeaddrinfo(found);
    if (conn.sock >= 0)
        std::fprintf(stderr, "fujinet: connected to %s:%s\n", conn.host.c_str(), conn.port.c_str());
}

void trs80m1_fujinet_device::link_receive()
{
    state &conn = *m_state;
    u8 buf[2048];
    while (conn.sock >= 0)
    {
        ssize_t got = ::recv(conn.sock, buf, sizeof(buf), 0);
        if (got > 0)
        {
            fuji_iolink_rx(&conn.card.link, buf, size_t(got), now_us());
            continue;
        }
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return;
        ::close(conn.sock);
        conn.sock = -1;
    }
}

TIMER_CALLBACK_MEMBER(trs80m1_fujinet_device::service)
{
    state &conn = *m_state;
    m1_card_t *card = &conn.card;
    // The switches stand for the card's DIPs, and may change while it runs.
    if (forced_off() != card->forced_off)
        m1_card_force_off(card, forced_off());
    m1_card_take_save(card); // the NVRAM is written at exit
    link_connect();
    link_receive();
    fuji_iolink_set_up(&card->link, conn.sock >= 0, now_us());
    m1_card_service(card, now_us());

    // In sync mode emulated time waits for the link, as the real card's CPU
    // would wait the same wall-clock time for the ESP32.
    auto give_up = std::chrono::steady_clock::now() + std::chrono::milliseconds(SYNC_WAIT_MS);
    while (conn.sync && conn.sock >= 0 && card->link.state == FUJI_LS_WAIT &&
           std::chrono::steady_clock::now() < give_up)
    {
        pollfd pfd = {conn.sock, POLLIN, 0};
        ::poll(&pfd, 1, 10);
        link_receive();
        m1_card_service(card, now_us());
    }
    m1_bus_idle(card);
    update_int();
}
