// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Atari 7800 FujiNet cartridge emulation

 See fujinet.h for the design; pico/atari-7800/README.md in fujinet-firmware
 for the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"
#include "speaker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, compiled in as C++ (apply.sh copies
// them next to this file), so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujitcp.h"
#include "fujiconfigrom.h"
#include "a78loaderrom.h"
#include "a78bootblk.h"

DEFINE_DEVICE_TYPE(A78_FUJINET, a78_fujinet_device, "a78_fujinet", "Atari 7800 FujiNet Cartridge")

// fujimail's port is C function pointers with no context argument; one slot,
// one cart, as on the hardware.
static a78_fujinet_device *s_fujinet = nullptr;

#define STORE_RAM_MAX  (160u * 1024)
#define STORE_MAX      0x80000u

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned offset, uint8_t value) { s_fujinet->poke(offset, value); }
static void c_paint(const uint8_t *src, uint8_t fill) { s_fujinet->paint(src, fill); }
static void c_window(uint16_t word) { s_fujinet->window(word); }
static bool c_link_up() { return fujitcp_active(); }
static uint8_t c_stream_open(int stream, uint32_t size) { return s_fujinet->stream_open(stream, size); }
static void c_stream_write(int stream, const uint8_t *chunk, unsigned len) { s_fujinet->stream_write(stream, chunk, len); }
static uint8_t c_stream_close(int stream, uint32_t got, bool aborted) { return s_fujinet->stream_close(stream, got, aborted); }
static void c_arm_swap() { s_fujinet->arm_swap(); }
static uint32_t s_now_ms;
static uint32_t c_now_ms() { return s_now_ms; }

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

a78_fujinet_device::a78_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, A78_FUJINET, tag, owner, clock)
	, device_a78_cart_interface(mconfig, *this)
	, m_pokey(*this, "pokey")
	, m_mode_out(*this, "fujinet_mode")
	, m_handover_out(*this, "fujinet_handover")
{
}

a78_fujinet_device::~a78_fujinet_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void a78_fujinet_device::device_add_mconfig(machine_config &config)
{
	SPEAKER(config, "fujinet_pokey").front_center();
	POKEY(config, m_pokey, DERIVED_CLOCK(1, 1)).add_route(ALL_OUTPUTS, "fujinet_pokey", 1.00);
}

void a78_fujinet_device::device_start()
{
	s_fujinet = this;

	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_sramdump = getenv("FUJINET_SRAMDUMP");
	m_cpu = dynamic_cast<cpu_device *>(machine().root_device().subdevice("maincpu"));

	m_sram.assign(A78MAP_SRAM_SIZE, 0x00);
	m_store[0].reserve(STORE_MAX);
	m_store[1].reserve(STORE_MAX);
	std::memset(m_arena, 0, sizeof m_arena);
	std::memset(m_hsc_ram, 0xFF, sizeof m_hsc_ram);

	m_load_port.poke = c_poke;
	m_load_port.paint = c_paint;
	m_load_port.window = c_window;
	m_hsc_port.transact = fujitcp_transact;
	m_hsc_port.link_up = c_link_up;
	m_hsc_port.now_ms = c_now_ms;

	m_service_timer = timer_alloc(FUNC(a78_fujinet_device::service), this);

	// No save_item: the protocol state lives in the shared C service's globals.
}

void a78_fujinet_device::load_file(std::vector<uint8_t> &out, const char *path)
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

// The image in the SRAM as the loader would leave it, and the console's own
// BIOS starts it: the side of an A/B test that is the FujiNet cart.
void a78_fujinet_device::direct_boot(const std::vector<uint8_t> &img)
{
	static uint8_t scratch[0x400];
	a78map_plan_t plan;
	fuji_load_t l{};
	fuji_slice_t s;

	if (a78map_plan(img.data(), uint32_t(img.size()), getenv("FUJINET_MAPPER"), &plan) != A78MAP_OK)
	{
		fprintf(stderr, "fujinet: FUJINET_IMAGE refused; serving the boot block\n");
		return;
	}
	std::fill(m_sram.begin(), m_sram.end(), 0xA5);
	l.base = img.data();
	l.plan = plan;
	l.hsc = m_hsc_on && !m_hsc_rom.empty();
	l.hsc_rom = m_hsc_rom.empty() ? nullptr : m_hsc_rom.data();
	l.hsc_ram = m_hsc_ram;
	for (unsigned n = 0, k = fuji_load_slices(&plan, l.hsc); n < k; n++)
	{
		fuji_load_slice(&l, n, &s, scratch);
		uint8_t *dst = &m_sram[uint32_t(s.page) * A78MAP_PAGE_SIZE + s.k * 0x400u];
		if (s.src)
			std::memcpy(dst, s.src, 0x400);
		else
			std::memset(dst, s.fill, 0x400);
	}
	a78map_init(&m_loader.next, &plan, l.hsc);
	m_loader.plan = plan;
	m_loader.next_mode = plan.claim ? FN_MODE_APP : FN_MODE_GAME;
	m_loader.next_pokey = plan.pokey;
	m_loader.next_hsc = l.hsc;
	flip();
	m_direct = true;
	fprintf(stderr, "fujinet: direct kind=%s crc=%08X size=%u claim=%d hsc=%d\n",
			a78map_kind_name(plan.kind), plan.crc, plan.size, plan.claim, l.hsc);
}

void a78_fujinet_device::device_reset()
{
	address_space &space = m_cpu->space(AS_PROGRAM);

	// MAME's 7800 gives a stock cart $4000-$FFFF only; the cart's other
	// ranges are installed here, after the driver's own machine_start.
	space.install_readwrite_handler(0x0400, 0x047f,
			read8sm_delegate(*this, NAME([this] (offs_t offset) { return bus_read(uint16_t(0x0400 + offset)); })),
			write8sm_delegate(*this, NAME([this] (offs_t offset, u8 data) { bus_write(uint16_t(0x0400 + offset), data); })));
	space.install_readwrite_handler(0x0600, 0x17ff,
			read8sm_delegate(*this, NAME([this] (offs_t offset) { return bus_read(uint16_t(0x0600 + offset)); })),
			write8sm_delegate(*this, NAME([this] (offs_t offset, u8 data) { bus_write(uint16_t(0x0600 + offset), data); })));
	space.install_readwrite_handler(0x3000, 0x3fff,
			read8sm_delegate(*this, NAME([this] (offs_t offset) { return bus_read(uint16_t(0x3000 + offset)); })),
			write8sm_delegate(*this, NAME([this] (offs_t offset, u8 data) { bus_write(uint16_t(0x3000 + offset), data); })));

	if (m_init_done)
	{
		// A reset is a power cycle (the 7800 has no reset line): the boot
		// block again, or the same image for a direct run. The mailbox, the
		// store and the HSC's RAM survive, as they do on a USB-powered cart.
		fuji_load_abort(&m_loader);
		to_boot();
		if (m_direct)
			direct_boot(m_direct_img);
		return;
	}
	m_init_done = true;

	fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
	fujimail_paint();

	// Every TIA write reaches INPTCTRL until it locks; the console's own
	// decode keeps them from the cart, so they come off a tap.
	m_tap_tia = space.install_write_tap(0x0000, 0x03ff, "fujinet_tia",
			[this](offs_t offset, u8 &data, u8)
			{
				if (machine().side_effects_disabled() || !a78_in_inptctrl(uint16_t(offset)))
					return;
				if (a78_inptctrl_write(&m_bus, data))
				{
					flip();
					fuji_load_event(&m_loader, A78_W_GO_BIOS);
					m_pokey->reset();
				}
			}, &m_tap_tia);
	// Only the PAL BIOS runs from $C000-$EFFF.
	m_tap_bios = space.install_read_tap(0xc000, 0xefff, "fujinet_bios",
			[this](offs_t, u8 &, u8)
			{
				if (!machine().side_effects_disabled() && m_bus.bootblk)
					m_loader.tv = FN_TV_PAL;
			}, &m_tap_bios);
	if (!strcmp(machine().system().name, "a7800p"))
		m_loader.tv = FN_TV_PAL;

	const char *hsc = getenv("FUJINET_HSC");
	if (hsc)
	{
		load_file(m_hsc_rom, hsc);
		m_hsc_on = m_hsc_rom.size() == A78MAP_HSC_ROM_SIZE;
		if (!m_hsc_on)
			m_hsc_rom.clear();
	}
	m_loader.hsc_rom = m_hsc_rom.empty() ? nullptr : m_hsc_rom.data();
	m_loader.hsc_ram = m_hsc_ram;
	m_loader.hsc_on = m_hsc_on;

	// CONFIG: baked, or a claimed client from -cart; an unclaimed -cart is
	// a game, run direct.
	m_config.assign(_configrom, _configrom + FUJI_CONFIGROM_SIZE);
	auto *slot = dynamic_cast<device_image_interface *>(owner());
	if (slot && slot->exists())
	{
		std::vector<uint8_t> cart;
		a78map_plan_t plan;

		load_file(cart, slot->filename());
		if (!cart.empty() && a78map_plan(cart.data(), uint32_t(cart.size()), nullptr, &plan) == A78MAP_OK)
		{
			if (plan.claim)
			{
				m_config.assign(cart.begin() + plan.offset, cart.end());
				fprintf(stderr, "fujinet: %u-byte client in place of CONFIG\n", unsigned(m_config.size()));
			}
			else if (!getenv("FUJINET_IMAGE"))
				m_direct_img = cart;
		}
	}
	{
		a78map_plan_t cp;

		if (a78map_plan(m_config.data(), uint32_t(m_config.size()), nullptr, &cp) != A78MAP_OK)
			std::memset(&cp, 0, sizeof cp);
		m_loader.port = &m_load_port;
		m_loader.bus = &m_bus;
		fuji_load_init(&m_loader, m_config.data(), &cp);
	}
	hsc_init(&m_hsc, &m_hsc_port, m_hsc_ram, &m_hsc_dirty);

	to_boot();
	if (getenv("FUJINET_IMAGE"))
		load_file(m_direct_img, getenv("FUJINET_IMAGE"));
	if (!m_direct_img.empty())
	{
		// A game alone needs no FujiNet: no socket, so A/B runs can go in
		// parallel against fujinet-pc's single BoIP client.
		direct_boot(m_direct_img);
		if (!m_loader.plan.claim)
			return;
	}
	fujitcp_init(nullptr);
	m_service_timer->adjust(attotime::from_msec(50), 0, attotime::from_msec(50));
}

/*-------------------------------------------------
    the modes, as core1 switches them
-------------------------------------------------*/

void a78_fujinet_device::to_boot()
{
	a78_bus_reset(&m_bus);
	m_live = nullptr;
	std::memset(m_load_slots, 0, sizeof m_load_slots);
	m_mode_out = FN_MODE_BOOT;
}

void a78_fujinet_device::to_load()
{
	a78_bus_load(&m_bus);
	m_live = nullptr;
	std::memset(m_load_slots, 0, sizeof m_load_slots);
	m_arena[FN_R_LOAD_STATE] = FN_LOAD_IDLE;
	m_mode_out = FN_MODE_LOAD;
}

void a78_fujinet_device::flip()
{
	m_live = &m_loader.next;
	a78_bus_run(&m_bus, m_loader.next_mode, m_loader.next_pokey, m_loader.next_hsc);
	m_mode_out = m_loader.next_mode;                // for the soak harness
	if (m_debug)
		fprintf(stderr, "fujinet: flip to mode %u (%s)\n", m_bus.mode,
				a78map_kind_name(m_live->plan.kind));
}

uint16_t a78_fujinet_device::slot_word(uint16_t a) const
{
	switch (m_bus.mode)
	{
	case FN_MODE_LOAD:
		return m_load_slots[a >> 13];
	case FN_MODE_GAME:
	case FN_MODE_APP:
		return m_live ? m_live->slot[a >> 13] : 0;
	default:
		return 0;
	}
}

/*-------------------------------------------------
    the bus
-------------------------------------------------*/

uint8_t a78_fujinet_device::bus_read(uint16_t a)
{
	uint16_t w = slot_word(a);

	if (a78_glue_oe(true, w, a, true))
	{
		uint32_t lo = a & 0x1FFF;
		if (!a78_glue_a8(w, a))
			lo &= ~0x100u;
		return m_sram[(uint32_t(w & A78S_PAGE_MASK) << 13) | lo];
	}
	unsigned off;
	switch (a78_read_kind(&m_bus, a, &off))
	{
	case A78_R_BOOTBLK: return _bootblk[off];
	case A78_R_LOADER:  return _loaderrom[off];
	case A78_R_ARENA:   return m_arena[off];
	case A78_R_POKEY:   return m_pokey->read(off);
	default:
		// as MAME's own carts read: $FF from the slot, open bus below it
		return a < 0x4000 ? m_cpu->space(AS_PROGRAM).unmap() : 0xff;
	}
}

void a78_fujinet_device::bus_write(uint16_t a, uint8_t data)
{
	uint16_t w;
	int kind;

	if (machine().side_effects_disabled())
		return;
	w = slot_word(a);
	if (a78_glue_we(true, w, a, false, true))
	{
		uint32_t lo = a & 0x1FFF;
		if (!a78_glue_a8(w, a))
			lo &= ~0x100u;
		m_sram[(uint32_t(w & A78S_PAGE_MASK) << 13) | lo] = data;
	}
	kind = a78_write_kind(&m_bus, a);
	switch (kind)
	{
	case A78_W_MAPPER:
		if (m_live)
			a78map_write(m_live, a, data);
		break;
	case A78_W_HSC:
		m_hsc_ram[a & (A78MAP_HSC_RAM_SIZE - 1)] = data;
		m_hsc_dirty |= 1u << ((a & (A78MAP_HSC_RAM_SIZE - 1)) >> 6);
		break;
	case A78_W_POKEY:
		m_pokey->write(a & 0x0F, data);
		break;
	case A78_W_MAILBOX:
		mailbox_event(uint16_t(a - FN_ARENA_BASE), data);
		break;
	case A78_W_SWAP:
	case A78_W_CONFIG:
		to_load();
		fuji_load_event(&m_loader, kind);
		break;
	case A78_W_GO:
		flip();
		fuji_load_event(&m_loader, A78_W_GO);
		m_pokey->reset();
		break;
	case A78_W_GO_BIOS:
		m_bus.go_bios = true;
		break;
	default:
		break;
	}
}

uint8_t a78_fujinet_device::read_40xx(offs_t offset) { return bus_read(uint16_t(0x4000 + offset)); }
void a78_fujinet_device::write_40xx(offs_t offset, uint8_t data) { bus_write(uint16_t(0x4000 + offset), data); }

/*-------------------------------------------------
    the mailbox
-------------------------------------------------*/

void a78_fujinet_device::dump_sram()
{
	char path[512];

	snprintf(path, sizeof path, "%s.sram", m_sramdump);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(m_sram.data(), 1, m_sram.size(), f);
	fclose(f);
}

void a78_fujinet_device::poke(unsigned offset, uint8_t value)
{
	if (offset < FN_R_PAINT_END)
		m_arena[offset] = value;
	if (offset == FN_R_HANDOVER)
		m_handover_out = value;                // for the soak harness
	if (offset == FN_R_LOAD_STATE && value == FN_LOAD_DONE)
	{
		const a78map_plan_t &p = m_loader.plan;
		fprintf(stderr, "fujinet: loaded kind=%s crc=%08X size=%u claim=%d hsc=%d handover=%s\n",
				a78map_kind_name(p.kind), p.crc, p.size, p.claim, m_loader.hsc,
				m_loader.handover == FN_HO_BIOS ? "BIOS" : "direct");
		if (m_sramdump)
			dump_sram();
	}
}

void a78_fujinet_device::paint(const uint8_t *src, uint8_t fill)
{
	if (src)
		std::memcpy(m_arena + FN_R_DATA, src, FN_R_SLICE_LEN);
	else
		std::memset(m_arena + FN_R_DATA, fill, FN_R_SLICE_LEN);
}

bool a78_fujinet_device::own_register(unsigned reg, uint8_t data)
{
	switch (reg)
	{
	case FN_REG_SLICE_ACK:
		fuji_load_ack(&m_loader);
		return true;
	case FN_REG_TV:
		m_loader.tv = data ? FN_TV_PAL : FN_TV_NTSC;
		return true;
	case FN_REG_HSC:
		if (data == FN_HSCOP_INSTALL)
		{
			const a78map_plan_t &p = m_loader.staged_plan;

			if (!m_loader.have_staged || p.kind != A78MAP_HSC || p.size != A78MAP_HSC_ROM_SIZE)
				return true;
			m_hsc_rom.assign(m_loader.staged_base + p.offset,
					m_loader.staged_base + p.offset + A78MAP_HSC_ROM_SIZE);
			fuji_load_unstage(&m_loader);
			poke(FN_R_STAGED, 0);
			m_hsc_on = true;
			fprintf(stderr, "fujinet: HSC ROM installed\n");
		}
		else if (data == FN_HSCOP_FORGET)
		{
			m_hsc_rom.clear();
			m_hsc_on = false;
		}
		else
			m_hsc_on = data == FN_HSCOP_ON && !m_hsc_rom.empty();
		m_loader.hsc_rom = m_hsc_rom.empty() ? nullptr : m_hsc_rom.data();
		m_loader.hsc_on = m_hsc_on;
		return true;
	default:
		return false;
	}
}

// One hotspot write, decoded as fujinet.c does on the cart.
void a78_fujinet_device::mailbox_event(uint16_t offset, uint8_t data)
{
	unsigned page = offset & FN_H_PAGE_MASK;
	unsigned low = offset & 0xFF;

	if (page == FN_H_REGSEL)
	{
		if (own_register(low, data))
			return;
		if (low >= 0x80)
			return;
		fujimail_read_hotspot(uint16_t(FN_H_REGSEL + low));
		fujimail_read_hotspot(uint16_t(FN_H_REGDATA + data));
	}
	else if (page == FN_H_DATA)
	{
		fujimail_read_hotspot(uint16_t(FN_H_DATA + data));
	}
}

// core0's loop, every 50 ms of emulated time.
TIMER_CALLBACK_MEMBER(a78_fujinet_device::service)
{
	uint8_t v = 0;

	s_now_ms = uint32_t(machine().time().as_double() * 1000.0);
	if (!m_hsc_rom.empty())
	{
		if (!m_hsc.restored)
		{
			if (m_bus.mode == FN_MODE_BOOT || m_bus.mode == FN_MODE_APP)
			{
				hsc_restore(&m_hsc);
				if (m_debug)
					fprintf(stderr, "fujinet: HSC RAM restored (sd=%d): %02X %02X %02X %02X\n",
							m_hsc.sd_ok, m_hsc_ram[0], m_hsc_ram[1], m_hsc_ram[2], m_hsc_ram[3]);
			}
		}
		else
			hsc_service(&m_hsc, m_bus.mode == FN_MODE_GAME);
		v |= FN_HSC_ROM;
	}
	if (m_hsc_on)
		v |= FN_HSC_ON;
	if (m_hsc.sd_ok)
		v |= FN_HSC_SD;
	if (m_hsc_dirty || m_hsc.pending)
		v |= FN_HSC_DIRTY;
	poke(FN_R_HSC, v);
	poke(FN_R_TV, m_loader.tv);
	poke(FN_R_INPTCTRL, m_bus.inptctrl);
	poke(FN_R_INPT_LOCK, m_bus.inpt_locked ? 1 : 0);
	poke(FN_R_MODE, m_bus.mode);
	poke(FN_R_LINK, fujitcp_active() ? 1 : 0);
}

/*-------------------------------------------------
    push streams: fuji_store's two tiers
-------------------------------------------------*/

uint8_t a78_fujinet_device::stream_open(int stream, uint32_t size)
{
	if (stream != FN_STREAM_ROM)
	{
		m_cfg.clear();
		return 0;
	}
	uint8_t err = a78map_gate(size);
	if (err)
		return err;
	fuji_load_unstage(&m_loader);
	poke(FN_R_STAGED, 0);
	m_open_tier = -1;
	if (size <= STORE_RAM_MAX && !fuji_load_busy(&m_loader, m_store[0].data()))
		m_open_tier = 0;
	else if (size <= STORE_MAX && !fuji_load_busy(&m_loader, m_store[1].data()))
		m_open_tier = 1;
	else
		return size <= STORE_MAX ? FN_BOOT_ERR_STOREBUSY : FN_BOOT_ERR_TOOBIG;
	m_store[m_open_tier].clear();
	return 0;
}

void a78_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	if (stream != FN_STREAM_ROM)
	{
		m_cfg.insert(m_cfg.end(), chunk, chunk + len);
		return;
	}
	if (m_open_tier >= 0 && m_store[m_open_tier].size() + len <= STORE_MAX)
		m_store[m_open_tier].insert(m_store[m_open_tier].end(), chunk, chunk + len);
}

void a78_fujinet_device::publish_staged(const a78map_plan_t &p)
{
	uint8_t ok = m_loader.tv == FN_TV_PAL ? A78_BIOSOK_PAL : A78_BIOSOK_NTSC;
	uint8_t v = FN_STAGED_READY;

	if (p.claim)
		v |= FN_STAGED_CLAIM;
	if (p.biosok & ok)
		v |= FN_STAGED_BIOSOK;
	if (p.kind == A78MAP_HSC)
		v |= FN_STAGED_HSCROM;
	poke(FN_R_STAGED_KIND, p.kind);
	for (unsigned i = 0; i < 4; i++)
		poke(FN_R_STAGED_CRC + i, uint8_t(p.crc >> (8 * i)));
	poke(FN_R_STAGED, v);
}

uint8_t a78_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	a78map_plan_t plan;
	int tier = m_open_tier;

	if (stream != FN_STREAM_ROM)
	{
		m_cfg_mapper.clear();
		std::string text(m_cfg.begin(), m_cfg.end());
		size_t at = text.find("mapper=");
		if (!aborted && at != std::string::npos)
		{
			size_t end = text.find_first_of(" \r\n", at + 7);
			m_cfg_mapper = text.substr(at + 7, end == std::string::npos ? std::string::npos : end - at - 7);
		}
		return 0;
	}
	m_open_tier = -1;
	if (aborted || tier < 0 || m_store[tier].empty())
	{
		m_cfg_mapper.clear();
		return 0;
	}
	int err = a78map_plan(m_store[tier].data(), got, m_cfg_mapper.empty() ? nullptr : m_cfg_mapper.c_str(), &plan);
	m_cfg_mapper.clear();
	if (err == A78MAP_ETOOBIG)
		return FN_BOOT_ERR_TOOBIG;
	if (err != A78MAP_OK)
	{
		fprintf(stderr, "fujinet: pushed image (%u bytes) is not mappable (%d)\n", got, err);
		return FN_BOOT_ERR_NOMAP;
	}
	fprintf(stderr, "fujinet: staged kind=%s crc=%08X size=%u claim=%d biosok=%u\n",
			a78map_kind_name(plan.kind), plan.crc, plan.size, plan.claim, plan.biosok);
	fuji_load_stage(&m_loader, m_store[tier].data(), &plan);
	publish_staged(plan);
	return 0;
}

void a78_fujinet_device::arm_swap()
{
	fuji_load_arm(&m_loader);
}
