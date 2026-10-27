// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Fairchild Channel F FujiNet Videocart emulation

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

// The cart firmware's sources, copied in by apply.sh and compiled as C++ for
// MAME's forced precompiled header, so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "chfmap.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(CHANF_ROM_FUJINET, channelf_fujinet_cartridge_device, "chanf_fujinet", "Channel F FujiNet Videocart")

// fujimail's port callbacks take no context argument.
static channelf_fujinet_cartridge_device *s_fujinet = nullptr;

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned offset, uint8_t value)
{
	s_fujinet->poke(offset, value);
}

static bool c_link_up()
{
	return fujitcp_active();
}

static uint8_t c_stream_open(int stream, uint32_t size)
{
	return s_fujinet->stream_open(stream, size);
}

static void c_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	s_fujinet->stream_write(stream, chunk, len);
}

static uint8_t c_stream_close(int stream, uint32_t got, bool aborted)
{
	return s_fujinet->stream_close(stream, got, aborted);
}

static void c_arm_swap()
{
	s_fujinet->arm_swap();
}

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
	c_poke,
	c_link_up,
	fujitcp_transact,
	fujitcp_send_bare,
	c_stream_open,
	c_stream_write,
	c_stream_close,
	c_arm_swap,
	nullptr,        // wait_link_ms: the socket round trip is synchronous
	nullptr,        // bootsel: nothing to reboot into under emulation
	c_on_txn,
	c_on_dbc,
};

static const fujimail_port_t mame_port_quiet = {
	c_poke,
	c_link_up,
	fujitcp_transact,
	fujitcp_send_bare,
	c_stream_open,
	c_stream_write,
	c_stream_close,
	c_arm_swap,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
};

/*-------------------------------------------------
    device
-------------------------------------------------*/

channelf_fujinet_cartridge_device::channelf_fujinet_cartridge_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, CHANF_ROM_FUJINET, tag, owner, clock)
	, device_channelf_cart_interface(mconfig, *this)
{
}

channelf_fujinet_cartridge_device::~channelf_fujinet_cartridge_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void channelf_fujinet_cartridge_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_bootdump = getenv("FUJINET_BOOTDUMP");
	m_live.assign(CHFMAP_STORE, 0xff);
	m_staged.assign(CHFMAP_STORE, 0xff);
	std::memset(m_arena, 0x00, sizeof m_arena);

	set_image();
	m_mem.ram = m_arena;
	m_mem.ram_base = FN_ARENA_BASE;
	m_mem.ram_size = sizeof m_arena;
	m_mem.mailbox = false;          // until an image claims it

	// No save_item: the protocol state is in fujimail's globals, so a live
	// mailbox does not survive save/load.
}

// Called at load and after every swap, so a swap always maps page 0.
void channelf_fujinet_cartridge_device::set_image()
{
	m_mem.rom = m_live.data();
	m_mem.rom_size = CHFMAP_WINDOW;
	m_mem.npages = m_live_np;
	m_mem.pages = m_live_np ? m_live.data() + CHFMAP_WINDOW : nullptr;
	m_mem.bank = m_mem.pages;
}

// The arena always answers, since the swap stub runs from it; only the
// mailbox decode comes and goes.
void channelf_fujinet_cartridge_device::set_mailbox(bool live)
{
	m_mailbox_live = live;
	m_mem.ram = m_arena;
	m_mem.mailbox = live;
}

void channelf_fujinet_cartridge_device::device_reset()
{
	chfmap_plan_t plan;

	// Once only: later resets (F3) must leave the running mailbox alone.
	if (m_init_done)
		return;
	m_init_done = true;

	if (m_rom && chfmap_plan(m_rom, m_rom_size, &plan) == CHFMAP_OK)
	{
		chfmap_apply(m_rom, &plan, m_live.data());
		m_live_np = plan.npages;
		set_image();
		set_mailbox(plan.mailbox_ok);
		if (plan.npages)
			fprintf(stderr, "fujinet: APPBANK image, %u pages; mailbox kept\n",
					unsigned(plan.npages));
		else
			fprintf(stderr, "fujinet: %u-byte image loaded, mailbox %s\n",
					unsigned(m_rom_size), plan.mailbox_ok ? "live" : "dead");
	}
	else
	{
		set_mailbox(false);
		fprintf(stderr, "fujinet: image is not mappable; serving open bus\n");
	}

	if (m_mailbox_live)
	{
		fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
		fujitcp_init(nullptr);
		fujimail_paint();
	}
	else
		fprintf(stderr, "fujinet: no claim signature at $47FC; running with the mailbox dead\n");
}

uint8_t channelf_fujinet_cartridge_device::read_rom(offs_t offset)
{
	// The driver maps $0800-$FFFF here. Reads have no side effects, so no
	// side_effects_disabled() guard.
	uint16_t const a = uint16_t(offset + FN_ROM_BASE);

	// The cart paints the tick from a timer; here it is emulated time.
	if (m_mailbox_live && (a == FN_ARENA_BASE + FN_R_TICK_LO || a == FN_ARENA_BASE + FN_R_TICK_HI))
	{
		uint16_t const t = uint16_t(machine().time().as_ticks(60));
		return (a == FN_ARENA_BASE + FN_R_TICK_LO) ? uint8_t(t) : uint8_t(t >> 8);
	}
	return chf_read(&m_mem, a);
}

void channelf_fujinet_cartridge_device::write_ram(offs_t offset, uint8_t data)
{
	if (machine().side_effects_disabled())
		return;

	uint8_t reg = 0, val = 0;
	chf_ev_t ev = chf_write(&m_mem, uint16_t(offset + FN_ROM_BASE), data, &reg, &val);

	switch (ev)
	{
	case CHF_EV_REG:
		// Expand into the REGSEL/REGDATA pair fujimail decodes.
		fujimail_read_hotspot(uint16_t(FN_H_REGSEL + reg));
		fujimail_read_hotspot(uint16_t(FN_H_REGDATA + val));
		break;

	case CHF_EV_TX:
		fujimail_read_hotspot(uint16_t(FN_H_DATA + val));
		break;

	case CHF_EV_SWAP:
		// The swap must happen inline in whatever serves the bus.
		if (m_swap_armed)
			do_swap();
		break;

	case CHF_EV_BANK:
		// chf_write already switched the page.
		if (m_debug)
			fprintf(stderr, "fujinet: page %u\n", unsigned(val));
		break;

	default:
		break;
	}
}

void channelf_fujinet_cartridge_device::poke(unsigned offset, uint8_t value)
{
	if (offset < sizeof m_arena)
		m_arena[offset] = value;
}

uint8_t channelf_fujinet_cartridge_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == FN_STREAM_ROM) ? chfmap_gate(size) : 0;

	if (err != 0)
		return err;
	if (stream == FN_STREAM_ROM)
		m_have_staged = false;      // its slot is about to be overwritten
	std::vector<uint8_t> &v = m_rx[stream & 1];
	v.clear();
	if (size)
		v.reserve(size);
	return 0;
}

void channelf_fujinet_cartridge_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];

	v.insert(v.end(), chunk, chunk + len);
}

uint8_t channelf_fujinet_cartridge_device::stream_close(int stream, uint32_t got, bool aborted)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];
	chfmap_plan_t plan;

	(void) got;         // v.size() is the byte count that actually landed

	if (m_bootdump && !v.empty())
	{
		char path[512];
		snprintf(path, sizeof path, "%s%s", m_bootdump, stream ? ".cfg" : ".rom");
		FILE *f = fopen(path, "wb");
		if (f)
		{
			fwrite(v.data(), 1, v.size(), f);
			fclose(f);
			fprintf(stderr, "fujinet: bootdump %s (%u bytes)\n", path, unsigned(v.size()));
		}
		else
			fprintf(stderr, "fujinet: cannot write %s\n", path);
	}

	if (stream != FN_STREAM_ROM)
	{
		// The .cfg sibling: no mappers here, so it is dropped.
		v.clear();
		return 0;
	}

	if (aborted)
	{
		v.clear();
		return 0;
	}

	if (v.empty() || chfmap_plan(v.data(), uint32_t(v.size()), &plan) != CHFMAP_OK)
	{
		v.clear();
		return FN_BOOT_ERR_NOMAP;
	}

	std::fill(m_staged.begin(), m_staged.end(), 0xff);
	chfmap_apply(v.data(), &plan, m_staged.data());
	v.clear();
	m_staged_plan = plan;
	m_staged_claims = plan.mailbox_ok;
	m_have_staged = true;
	return 0;
}

void channelf_fujinet_cartridge_device::arm_swap()
{
	if (m_have_staged)
		m_swap_armed = true;
}

void channelf_fujinet_cartridge_device::do_swap()
{
	std::swap(m_live, m_staged);
	m_live_np = m_staged_plan.npages;
	set_image();
	m_swap_armed = false;
	m_have_staged = false;
	set_mailbox(m_staged_claims);
	if (m_mailbox_live)
		fujimail_paint();
	fprintf(stderr, "fujinet: swapped in the staged image (%u bytes); mailbox %s for this session\n",
			unsigned(m_staged_plan.size), m_mailbox_live ? "kept" : "disabled");
}
