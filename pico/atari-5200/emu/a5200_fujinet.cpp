// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Atari 5200 FujiNet cartridge emulation

 See a5200_fujinet.h for the design; pico/atari-5200/README.md in
 fujinet-firmware for the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "a5200_fujinet.h"

#include "video.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, compiled in as C++ (apply.sh copies
// them next to this file), so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujitcp.h"
#include "fujiconfigrom.h"

DEFINE_DEVICE_TYPE(A5200_FUJINET, a5200_fujinet_device, "a5200_fujinet", "Atari 5200 FujiNet Cartridge")

// fujimail's port is C function pointers with no context argument; one slot,
// one cart, as on the hardware.
static a5200_fujinet_device *s_fujinet = nullptr;

// set on the worker thread: its pokes queue for the CPU thread
static thread_local bool s_on_worker = false;

// the event queue's marker for a swap the CPU thread made
static constexpr uint16_t EV_SWAPPED = 0xFFFF;

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned offset, uint8_t value) { s_fujinet->poke(offset, value); }
static bool c_link_up() { return fujitcp_active(); }
static uint8_t c_stream_open(int stream, uint32_t size) { return s_fujinet->stream_open(stream, size); }
static void c_stream_write(int stream, const uint8_t *chunk, unsigned len) { s_fujinet->stream_write(stream, chunk, len); }
static uint8_t c_stream_close(int stream, uint32_t got, bool aborted) { return s_fujinet->stream_close(stream, got, aborted); }
static void c_arm_swap() { s_fujinet->arm_swap(); }

static void c_on_txn(const fujimail_txn_t *t)
{
	char txt[40];
	unsigned k, m = 0;

	for (k = 0; k < t->rxlen && m < sizeof txt - 1; k++)
	{
		uint8_t c = t->rx[k];
		if (c == 0)
			break;
		txt[m++] = (c >= 0x20 && c < 0x7F) ? char(c) : '.';
	}
	txt[m] = '\0';
	fprintf(stderr,
			"fujinet: dev=%02X cmd=%02X nparam=%u txlen=%u seq=%u"
			" -> err=%d reply=%02X rxlen=%u%s%s%s\n",
			t->device, t->command, t->nparam, t->txlen, t->seq,
			t->status, t->reply_cmd, t->rxlen,
			m ? " \"" : "", txt, m ? "\"" : "");
}

static void c_on_dbc(fujimail_dbc_ev_t ev, int stream, uint32_t expect, unsigned got, bool aborted)
{
	if (ev == FUJIMAIL_DBC_OPEN)
		fprintf(stderr, "fujinet: DBC open stream=%d size=%u\n", stream, expect);
	else
		fprintf(stderr, "fujinet: DBC close stream=%d got=%u%s\n",
				stream, got, aborted ? " ABORTED" : "");
}

static const fujimail_port_t mame_port = {
	c_poke, c_link_up, fujitcp_transact, fujitcp_send_bare,
	c_stream_open, c_stream_write, c_stream_close, c_arm_swap,
	nullptr, nullptr, c_on_txn, c_on_dbc,
};

static const fujimail_port_t mame_port_quiet = {
	c_poke, c_link_up, fujitcp_transact, fujitcp_send_bare,
	c_stream_open, c_stream_write, c_stream_close, c_arm_swap,
	nullptr, nullptr, nullptr, nullptr,
};

/*-------------------------------------------------
    device
-------------------------------------------------*/

a5200_fujinet_device::a5200_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, A5200_FUJINET, tag, owner, clock)
	, device_a5200_cart_interface(mconfig, *this)
	, m_swaps_out(*this, "fujinet_swaps")
{
}

a5200_fujinet_device::~a5200_fujinet_device()
{
	worker_stop();
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void a5200_fujinet_device::cart_map(address_map &map)
{
	map(0x0000, 0x7fff).rw(FUNC(a5200_fujinet_device::read), FUNC(a5200_fujinet_device::write));
}

void a5200_fujinet_device::load_file(std::vector<uint8_t> &out, const char *path)
{
	out.clear();
	if (!path)
		return;
	FILE *f = fopen(path, "rb");
	if (!f)
	{
		fprintf(stderr, "fujinet: cannot open %s\n", path);
		return;
	}
	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		out.insert(out.end(), buf, buf + n);
	fclose(f);
}

void a5200_fujinet_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_merge = getenv("FUJINET_MERGE") && atoi(getenv("FUJINET_MERGE"));
	m_viewdump = getenv("FUJINET_VIEWDUMP");
	m_cpu = dynamic_cast<cpu_device *>(machine().root_device().subdevice("maincpu"));
	m_buf[0].assign(A52MAP_VIEW_MAX, 0xFF);
	m_buf[1].assign(A52MAP_VIEW_MAX, 0xFF);

	// No save_item: the protocol state lives in the shared C service's globals.
}

void a5200_fujinet_device::device_stop()
{
	worker_stop();
}

// Power-on. The -cart image exists by now; device_start is too early for it.
void a5200_fujinet_device::device_reset()
{
	std::vector<uint8_t> cart;
	a52map_plan_t plan;

	worker_stop();
	if (m_rom && m_rom_size)
		cart.assign(m_rom, m_rom + m_rom_size);
	load_file(m_direct, getenv("FUJINET_IMAGE"));
	m_boot.assign(_configrom, _configrom + FUJI_CONFIGROM_SIZE);
	if (m_direct.empty() && !cart.empty())
	{
		if (a52map_plan(cart.data(), uint32_t(cart.size()), nullptr, &plan) == A52MAP_OK && plan.claim
			&& cart.size() == A52MAP_WINDOW)
			m_boot = cart;
		else
			m_direct = cart;
	}

	std::memset(m_arena, 0, FN_H_REGSEL);
	std::memset(m_arena + FN_H_REGSEL, 0xFF, FN_ARENA_SIZE - FN_H_REGSEL);
	m_arena[FN_H_REGSEL + FN_HOT_STUB] = 0x6C;
	m_arena[FN_H_REGSEL + FN_HOT_STUB + 1] = 0xFC;
	m_arena[FN_H_REGSEL + FN_HOT_SWAP] = 0xFF;
	fuji_load_init(&m_load, m_buf[0].data(), m_buf[1].data(), m_arena,
			m_boot.data(), uint32_t(m_boot.size()));
	m_last_off = ~offs_t(0);
	m_own_sel = 0xFF;
	m_dumps = 0;

	if (!m_direct.empty())
	{
		// served at power-on as if the cart had planned and swapped it in
		uint8_t *t = fuji_load_target(&m_load);
		uint32_t n = std::min<uint32_t>(uint32_t(m_direct.size()), A52MAP_IMAGE_MAX + A52MAP_CAR_HEADER);

		std::memcpy(t, m_direct.data(), n);
		if (fuji_load_commit(&m_load, n, getenv("FUJINET_MAPPER")) != 0)
			fprintf(stderr, "fujinet: FUJINET_IMAGE refused; serving CONFIG\n");
		else
		{
			fuji_load_arm(&m_load);
			(void)fuji_load_swap(&m_load);
			const a52map_plan_t &p = m_load.plan[m_load.live];
			fprintf(stderr, "fujinet: DIRECT %u bytes as %s (src %u)%s\n", unsigned(n),
					a52map_kind_name(p.kind), p.src, p.claim ? ", claimed" : "");
		}
	}
	m_view = fuji_load_live(&m_load);
	m_mailbox_mode = m_view->mailbox;
	m_swaps_out = m_load.swaps;
	fujitcp_close();                    // fujinet-pc's BoIP port takes one client
	if (m_mailbox_mode)
	{
		fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
		fujitcp_init(nullptr);
		fujimail_paint();
	}
	const char *async = getenv("FUJINET_ASYNC");
	m_async = m_mailbox_mode && (async ? atoi(async) != 0 : machine().video().throttled());
	if (m_async)
		m_worker = std::thread(&a5200_fujinet_device::worker_main, this);
	if (m_viewdump && !m_direct.empty())
		dump_view();
}

uint8_t a5200_fujinet_device::read(offs_t offset)
{
	if (m_async)
		drain_pokes();

	uint8_t data = a52_serve(m_view, offset);

	if (!machine().side_effects_disabled())
		access(offset);
	return data;
}

void a5200_fujinet_device::write(offs_t offset, uint8_t data)
{
	(void)data;
	if (!machine().side_effects_disabled())
		access(offset);
}

void a5200_fujinet_device::access(offs_t offset)
{
	if (m_merge && m_cpu)
	{
		uint64_t cyc = m_cpu->total_cycles();

		// a clockless cart cannot see two cycles at one address apart
		if (offset == m_last_off && cyc == m_last_cycle + 1)
		{
			m_last_cycle = cyc;
			return;
		}
		m_last_off = offset;
		m_last_cycle = cyc;
	}
	if (!(m_view->hot & (1u << (offset >> 11))))
		return;
	switch (a52_commit(m_view, offset))
	{
	case A52_EV_MAILBOX:
		hotspot(uint16_t(offset - FN_ARENA_OFF));
		break;
	case A52_EV_SWAP:
		{
			a52_view_t *nv;
			{
				std::lock_guard<std::mutex> g(m_loadlock);
				nv = fuji_load_swap(&m_load);
			}
			if (nv != m_view)
			{
				m_view = nv;
				swapped();
			}
		}
		break;
	default:
		break;
	}
}

// A hotspot event: to the worker, or served here when synchronous.
void a5200_fujinet_device::hotspot(uint16_t a)
{
	if (!m_async)
	{
		service(a);
		return;
	}
	{
		std::lock_guard<std::mutex> g(m_qlock);
		m_events.push_back(a);
	}
	m_qcv.notify_one();
}

void a5200_fujinet_device::service(uint16_t a)
{
	if (a != EV_SWAPPED)
	{
		mailbox_read(a);
		return;
	}
	uint8_t swaps, kind;
	bool mailbox;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		swaps = m_load.swaps;
		kind = m_load.plan[m_load.live].kind;
		mailbox = m_load.view[m_load.live].mailbox;
	}
	m_own_sel = 0xFF;
	if (mailbox)
		fujimail_paint();
	publish_staged();
	poke(FN_R_SWAPS, swaps);
	poke(FN_R_MODE, mailbox ? FN_MODE_APP : FN_MODE_GAME);
	poke(FN_R_MAPPER, kind);
}

void a5200_fujinet_device::worker_main()
{
	s_on_worker = true;
	for (;;)
	{
		uint16_t a;
		{
			std::unique_lock<std::mutex> g(m_qlock);
			m_qcv.wait(g, [this] { return m_stop || !m_events.empty(); });
			if (m_stop)
				return;
			a = m_events.front();
			m_events.pop_front();
		}
		service(a);
	}
}

// A transaction in flight is cut short, so a reset or exit never waits out
// its timeout.
void a5200_fujinet_device::worker_stop()
{
	if (!m_worker.joinable())
		return;
	{
		std::lock_guard<std::mutex> g(m_qlock);
		m_stop = true;
	}
	m_qcv.notify_one();
	fujitcp_abort();
	m_worker.join();
	m_events.clear();
	m_stop = false;
	std::lock_guard<std::mutex> g(m_pklock);
	m_pokes.clear();
	m_poked.store(false);
}

void a5200_fujinet_device::drain_pokes()
{
	if (!m_poked.load(std::memory_order_acquire))
		return;
	std::lock_guard<std::mutex> g(m_pklock);
	for (const auto &p : m_pokes)
		m_arena[p.first] = p.second;
	m_pokes.clear();
	m_poked.store(false, std::memory_order_relaxed);
}

void a5200_fujinet_device::mailbox_read(uint16_t a)
{
	unsigned page = a & FN_H_PAGE_MASK;

	if (page == FN_H_REGSEL)
		m_own_sel = (a & 0xFF) < 0x80 ? uint8_t(a & 0xFF) : m_own_sel;
	else if (page == FN_H_REGDATA && m_own_sel != 0xFF)
	{
		if (m_own_sel == FN_REG_CONFIG && (a & 0xFF) == FN_CONFIG_MAGIC)
		{
			{
				std::lock_guard<std::mutex> g(m_loadlock);
				fuji_load_stage_config(&m_load, m_boot.data(), uint32_t(m_boot.size()));
				fuji_load_arm(&m_load);
			}
			publish_staged();
		}
		m_own_sel = 0xFF;
	}
	fujimail_read_hotspot(a);
}

// The CPU thread's half of a swap; the status page is repainted by service().
void a5200_fujinet_device::swapped()
{
	a52map_plan_t p;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		p = m_load.plan[m_load.live];
	}

	fprintf(stderr, "fujinet: swapped in %s (%u bytes, crc %08X); mailbox %s\n",
			a52map_kind_name(p.kind), p.size, p.crc, m_view->mailbox ? "kept" : "off");
	m_swaps_out = m_load.swaps;
	hotspot(EV_SWAPPED);
	if (m_viewdump)
		dump_view();
}

void a5200_fujinet_device::dump_view()
{
	char path[1024];
	a52_view_t v = *m_view;     // a copy: reading must not move its banks

	snprintf(path, sizeof path, "%s%s", m_viewdump, m_dumps ? ".2" : "");
	FILE *f = fopen(path, "wb");
	if (!f)
	{
		fprintf(stderr, "fujinet: cannot write %s\n", path);
		return;
	}
	for (uint32_t o = 0; o < A52MAP_WINDOW; o++)
		fputc(a52_serve(&v, o), f);
	fclose(f);
	m_dumps++;
	fprintf(stderr, "fujinet: viewdump %s\n", path);
}

void a5200_fujinet_device::publish_staged()
{
	a52map_plan_t p;
	bool staged, staged_config, armed;
	uint8_t v = 0;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		p = m_load.plan[m_load.live ^ 1u];
		staged = m_load.staged;
		staged_config = m_load.staged_config;
		armed = m_load.armed;
	}

	if (staged)
	{
		v = FN_STAGED_READY;
		if (p.claim)
			v |= FN_STAGED_CLAIM;
		if (staged_config)
			v |= FN_STAGED_CONFIG;
	}
	poke(FN_R_STAGED_KIND, p.kind);
	for (unsigned i = 0; i < 4; i++)
		poke(FN_R_STAGED_CRC + i, uint8_t(p.crc >> (8 * i)));
	poke(FN_R_STAGED, v);
	poke(FN_R_ARMED, armed ? 1 : 0);
}

void a5200_fujinet_device::poke(unsigned offset, uint8_t value)
{
	if (offset >= FN_R_PAINT_END)
		return;
	if (!s_on_worker)
	{
		m_arena[offset] = value;
		return;
	}
	std::lock_guard<std::mutex> g(m_pklock);
	m_pokes.emplace_back(uint16_t(offset), value);
	m_poked.store(true, std::memory_order_release);
}

uint8_t a5200_fujinet_device::stream_open(int stream, uint32_t size)
{
	if (stream != FN_STREAM_ROM)
	{
		m_cfg.clear();
		return 0;
	}
	uint8_t err = a52map_gate(size);
	if (err)
		return err;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		m_push = fuji_load_target(&m_load);
	}
	m_push_len = 0;
	publish_staged();
	return 0;
}

void a5200_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	if (stream != FN_STREAM_ROM)
	{
		if (m_cfg.size() + len < 256)
			m_cfg.insert(m_cfg.end(), chunk, chunk + len);
		return;
	}
	if (m_push && m_push_len + len <= A52MAP_IMAGE_MAX + A52MAP_CAR_HEADER)
	{
		std::memcpy(m_push + m_push_len, chunk, len);
		m_push_len += len;
	}
}

uint8_t a5200_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	(void)got;
	if (stream != FN_STREAM_ROM)
	{
		// the .cfg sibling: its mapper= applies to the next image
		std::string text(m_cfg.begin(), m_cfg.end());
		size_t at = text.find("mapper=");

		m_cfg_mapper.clear();
		if (!aborted && at != std::string::npos)
		{
			at += 7;
			while (at < text.size() && !strchr(" \t\r\n", text[at]))
				m_cfg_mapper += text[at++];
		}
		return 0;
	}
	std::string mapper = m_cfg_mapper;
	m_cfg_mapper.clear();
	if (aborted || !m_push || m_push_len == 0)
	{
		m_push = nullptr;
		return 0;
	}
	if (!mapper.empty())
		fprintf(stderr, "fujinet: .cfg mapper=%s\n", mapper.c_str());
	uint8_t err;
	a52map_plan_t p;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		err = fuji_load_commit(&m_load, m_push_len, mapper.empty() ? nullptr : mapper.c_str());
		p = m_load.plan[m_load.live ^ 1u];
	}
	m_push = nullptr;
	publish_staged();
	if (!err)
	{
		fprintf(stderr, "fujinet: staged %u bytes as %s (src %u)%s\n", p.size,
				a52map_kind_name(p.kind), p.src, p.claim ? ", claimed" : "");
	}
	return err;
}

void a5200_fujinet_device::arm_swap()
{
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		fuji_load_arm(&m_load);
	}
	publish_staged();
}
