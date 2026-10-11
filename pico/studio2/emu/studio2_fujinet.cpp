// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 RCA Studio II FujiNet cartridge emulation

 See studio2_fujinet.h for the design; pico/studio2/README.md in
 fujinet-firmware for the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "studio2_fujinet.h"

#include "video.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, compiled in as C++ (apply.sh copies
// them next to this file), so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujitcp.h"
#include "fujiconfigrom.h"

DEFINE_DEVICE_TYPE(STUDIO2_FUJINET, studio2_fujinet_device, "studio2_fujinet", "RCA Studio II FujiNet Cartridge")

// fujimail's port is C function pointers with no context argument; one slot,
// one cart, as on the hardware.
static studio2_fujinet_device *s_fujinet = nullptr;

// set on the worker thread: its pokes queue for the CPU thread
static thread_local bool s_on_worker = false;

// the event queue's marker for a swap the CPU thread made
static constexpr uint16_t EV_SWAPPED = S2_RING_SWAP;

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

studio2_fujinet_device::studio2_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, STUDIO2_FUJINET, tag, owner, clock)
	, m_swaps_out(*this, "fujinet_swaps")
	, m_short_out(*this, "fujinet_shortlines")
{
}

studio2_fujinet_device::~studio2_fujinet_device()
{
	worker_stop();
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void studio2_fujinet_device::load_file(std::vector<uint8_t> &out, const char *path)
{
	out.clear();
	if (!path || !*path)
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

void studio2_fujinet_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_viewdump = getenv("FUJINET_VIEWDUMP");
	m_cpu = dynamic_cast<cpu_device *>(machine().root_device().subdevice("ic1"));
	m_bios = machine().root_device().memregion("ic1")->base();
	m_buf[0].assign(S2MAP_BUF_MAX, 0);
	m_buf[1].assign(S2MAP_BUF_MAX, 0);
	std::memset(m_arena, 0, sizeof m_arena);
	s2_text_init(&m_text, m_arena + FN_R_DATA);
	s2_bus_reset(&m_bus);

	// For the harnesses' eyes only (Lua reads these, not the address space).
	save_item(NAME(m_arena));
	save_item(NAME(m_text.raster));
	save_item(NAME(m_text.chars));
	save_item(NAME(m_text.inv));
	save_item(NAME(m_text.row));
	save_item(NAME(m_text.col));
	save_item(NAME(m_short_lines));
}

void studio2_fujinet_device::device_stop()
{
	worker_stop();
}

// Power-on. Every reset is one: CLEAR resets only the CPU and the 1861.
void studio2_fujinet_device::device_reset()
{
	const char *boot = getenv("FUJINET_BOOT");
	const char *image = getenv("FUJINET_IMAGE");

	m_active = getenv("FUJINET") || (boot && *boot) || (image && *image);
	if (!m_active)
		return;

	worker_stop();
	if (!m_ram)
	{
		memory_share *ram = machine().root_device().memshare("ram");
		if (!ram)
			fatalerror("studio2_fujinet: no console RAM share (re-run apply.sh)\n");
		m_ram = static_cast<uint8_t *>(ram->ptr());
	}
	load_file(m_direct, image);
	load_file(m_boot, boot);
	if (m_boot.empty())
		m_boot.assign(_configrom, _configrom + FUJI_CONFIGROM_SIZE);

	std::memset(m_arena, 0, FN_H_REGSEL);
	std::memset(m_arena + FN_H_REGSEL, 0xFF, FN_ARENA_SIZE - FN_H_REGSEL);
	m_arena[FN_H_REGSEL + FN_HOT_STUB] = 0xEC;
	m_arena[FN_H_REGSEL + FN_HOT_STUB + 1] = 0x70;
	m_arena[FN_H_REGSEL + FN_HOT_T] = 0x23;
	s2_text_init(&m_text, m_arena + FN_R_DATA);
	fuji_load_init(&m_load, m_buf[0].data(), m_buf[1].data(), m_arena, m_text.raster,
			m_boot.data(), uint32_t(m_boot.size()));
	s2_bus_reset(&m_bus);
	m_own_sel = 0xFF;
	m_dumps = 0;
	m_short_lines = 0;

	if (!m_direct.empty())
	{
		// served at power-on as if the cart had planned and swapped it in
		uint8_t *t = fuji_load_target(&m_load);
		uint32_t n = std::min<uint32_t>(uint32_t(m_direct.size()), S2MAP_BUF_MAX);

		std::memcpy(t, m_direct.data(), n);
		uint8_t err = fuji_load_commit(&m_load, n);
		if (err)
			fprintf(stderr, "fujinet: FUJINET_IMAGE refused (%u); serving CONFIG\n", err);
		else
		{
			fuji_load_arm(&m_load);
			(void)fuji_load_swap(&m_load);
			const s2map_plan_t &p = m_load.plan[m_load.live];
			fprintf(stderr, "fujinet: DIRECT %u bytes, %s, %u blocks%s\n", unsigned(n),
					p.st2 ? "ST2" : "raw", p.blocks, p.claim ? ", claimed" : "");
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
		m_worker = std::thread(&studio2_fujinet_device::worker_main, this);
	if (m_viewdump && !m_direct.empty())
		dump_view();

	// Every read crosses the edge; the pages the cart does not claim are
	// given back as the console would serve them.
	m_cpu->space(AS_PROGRAM).install_read_handler(0x0000, 0xffff,
			read8sm_delegate(*this, FUNC(studio2_fujinet_device::read)));
}

uint32_t studio2_fujinet_device::now_us() const
{
	return uint32_t(m_cpu->total_cycles() * 1000000ULL / m_cpu->clock());
}

// What the console serves where the cart does not drive: the BIOS, the RAM
// (on every A9 = 0 page, as studio2_map mirrors it), else open bus.
uint8_t studio2_fujinet_device::unclaimed(uint16_t a) const
{
	if (a < 0x0400)
		return m_bios[a];
	if (a >= 0x0800 && !(a & 0x0200))
		return m_ram[a & 0x01FF];
	return 0xFF;
}

uint8_t studio2_fujinet_device::read(offs_t offset)
{
	uint16_t a = uint16_t(offset);
	uint8_t data = 0xFF;

	if (m_async)
		drain_pokes();

	if (machine().side_effects_disabled())
	{
		uint8_t ty = m_view->type[a >> 8];
		if (ty == S2PG_NONE)
			return unclaimed(a);
		return ty == S2PG_RASTER ? 0 : m_view->page[a >> 8][a & 0xFF];
	}

	uint32_t t = now_us();
	unsigned r = s2_bus_read(&m_bus, m_view, a, t, &data);

	if (!(r & S2_DRIVE))
		data = unclaimed(a);
	else if (m_view->type[a >> 8] == S2PG_RASTER)
	{
		// a burst that ended short of 8 reads: a line the 1861 drew wrong
		if (m_bus.r_col == 0 && m_bus.r_line == m_rline + 1 && m_rcol != S2_LINE_BYTES - 1)
		{
			m_short_lines++;
			m_short_out = m_short_lines;
		}
		m_rline = m_bus.r_line;
		m_rcol = m_bus.r_col;
	}
	if (r & S2_HOT_EV)
		hotspot(a);
	if (r & S2_RESET)
	{
		s2_view_t *nv;
		{
			std::lock_guard<std::mutex> g(m_loadlock);
			nv = fuji_load_swap(&m_load);
		}
		if (nv != m_view)
		{
			m_view = nv;
			m_swaps_out = m_load.swaps;
			if (m_debug)
				fprintf(stderr, "fujinet: swap %u -> %s\n", m_load.swaps, nv->mailbox ? "app" : "game");
			if (m_viewdump)
				dump_view();
			hotspot(EV_SWAPPED);
		}
	}
	if (!m_async && m_mailbox_mode && (t & 0x3FFF) < 8)
		s2_text_tick(&m_text, t / 1000);
	return data;
}

// A hotspot event (or a swap): to the worker, or served here when synchronous.
void studio2_fujinet_device::hotspot(uint16_t a)
{
	if (!m_mailbox_mode)
		return;
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

// One hotspot read by console address: the text engine's, or the mailbox's
// (with the cart's own FN_REG_CONFIG beside fujimail's registers).
void studio2_fujinet_device::dispatch(uint16_t a)
{
	if (a >= FN_TEXT_BASE)
	{
		s2_text_event(&m_text, a);
		return;
	}
	uint16_t off = uint16_t(a - FN_ARENA_BASE);
	unsigned page = off & FN_H_PAGE_MASK;
	if (page == FN_H_REGSEL)
		m_own_sel = (off & 0xFF) < 0x80 ? uint8_t(off & 0xFF) : m_own_sel;
	else if (page == FN_H_REGDATA && m_own_sel != 0xFF)
	{
		if (m_own_sel == FN_REG_CONFIG && (off & 0xFF) == FN_CONFIG_MAGIC)
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
	fujimail_read_hotspot(off);
}

void studio2_fujinet_device::service(uint16_t a)
{
	if (a != EV_SWAPPED)
	{
		dispatch(a);
		return;
	}
	swapped();
}

// The new image is running. If it claims the mailbox it finds the interlock
// starting over and a blank screen, exactly as CONFIG does at power-on.
void studio2_fujinet_device::swapped()
{
	uint8_t swaps;
	bool mailbox;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		swaps = m_load.swaps;
		mailbox = m_load.view[m_load.live].mailbox;
	}
	m_own_sel = 0xFF;
	if (mailbox)
	{
		s2_text_init(&m_text, m_arena + FN_R_DATA);
		fujimail_paint();
	}
	publish_staged();
	poke(FN_R_SWAPS, swaps);
	poke(FN_R_MODE, mailbox ? FN_MODE_APP : FN_MODE_GAME);
}

void studio2_fujinet_device::worker_main()
{
	s_on_worker = true;
	for (;;)
	{
		uint16_t a;
		{
			std::unique_lock<std::mutex> g(m_qlock);
			m_qcv.wait_for(g, std::chrono::milliseconds(50), [this] { return m_stop || !m_events.empty(); });
			if (m_stop)
				return;
			if (m_events.empty())
			{
				g.unlock();
				auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count();
				s2_text_tick(&m_text, uint32_t(ms));
				continue;
			}
			a = m_events.front();
			m_events.pop_front();
		}
		service(a);
	}
}

// A transaction in flight is cut short, so a reset or exit never waits out
// its timeout.
void studio2_fujinet_device::worker_stop()
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

void studio2_fujinet_device::drain_pokes()
{
	if (!m_poked.load(std::memory_order_acquire))
		return;
	std::lock_guard<std::mutex> g(m_pklock);
	for (const auto &p : m_pokes)
		m_arena[p.first] = p.second;
	m_pokes.clear();
	m_poked.store(false, std::memory_order_relaxed);
}

// The 64K the CPU would read from the cart, unclaimed bytes $FF and the
// raster's pages 0: what Tier C compares against tools/s2plan.
void studio2_fujinet_device::dump_view()
{
	char path[1024];
	snprintf(path, sizeof path, "%s.%u", m_viewdump, m_dumps);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	for (uint32_t a = 0; a < 0x10000; a++)
	{
		uint8_t ty = m_view->type[a >> 8];
		fputc(ty == S2PG_NONE ? 0xFF : ty == S2PG_RASTER ? 0 : m_view->page[a >> 8][a & 0xFF], f);
	}
	fclose(f);
	m_dumps++;
	fprintf(stderr, "fujinet: viewdump %s\n", path);
}

void studio2_fujinet_device::publish_staged()
{
	s2map_plan_t p;
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
	for (unsigned i = 0; i < 4; i++)
		poke(FN_R_STAGED_CRC + i, uint8_t(p.crc >> (8 * i)));
	poke(FN_R_STAGED, v);
	poke(FN_R_ARMED, armed ? 1 : 0);
}

void studio2_fujinet_device::poke(unsigned offset, uint8_t value)
{
	s2_text_boot_poke(&m_text, offset, value);
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

uint8_t studio2_fujinet_device::stream_open(int stream, uint32_t size)
{
	if (stream != FN_STREAM_ROM)
		return 0;
	uint8_t err = s2map_gate(size);
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

void studio2_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	if (stream != FN_STREAM_ROM || !m_push)
		return;
	if (m_push_len + len > S2MAP_BUF_MAX)
		len = S2MAP_BUF_MAX - m_push_len;
	std::memcpy(m_push + m_push_len, chunk, len);
	m_push_len += len;
}

uint8_t studio2_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	(void)got;
	if (stream != FN_STREAM_ROM)
		return 0;
	if (aborted || !m_push || m_push_len == 0)
	{
		m_push = nullptr;
		return 0;
	}
	uint8_t err;
	s2map_plan_t p;
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		err = fuji_load_commit(&m_load, m_push_len);
		p = m_load.plan[m_load.live ^ 1u];
	}
	m_push = nullptr;
	publish_staged();
	if (!err)
		fprintf(stderr, "fujinet: staged %u bytes, %s, %u blocks%s\n", p.size,
				p.st2 ? "ST2" : "raw", p.blocks, p.claim ? ", claimed" : "");
	return err;
}

void studio2_fujinet_device::arm_swap()
{
	{
		std::lock_guard<std::mutex> g(m_loadlock);
		fuji_load_arm(&m_load);
	}
	publish_staged();
}
