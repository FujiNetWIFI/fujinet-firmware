// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Fairchild Channel F FujiNet Videocart emulation

 See fujinet.h for the design; pico/channelf/README.md in fujinet-firmware for
 the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own protocol sources, compiled in verbatim (copied
// next to this file by pico/channelf/emu/apply.sh -- as C++, to live with
// MAME's forced C++ precompiled header, so no extern "C").
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "chfmap.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(CHANF_ROM_FUJINET, channelf_fujinet_cartridge_device, "chanf_fujinet", "Channel F FujiNet Videocart")

// fujimail's port interface is C function pointers with no context argument,
// so the single device instance is reached through this. One cart slot, one
// cart: the constraint is real hardware's too.
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
	std::memset(m_window, 0xff, sizeof m_window);
	std::memset(m_staged, 0xff, sizeof m_staged);
	std::memset(m_arena, 0x00, sizeof m_arena);

	m_mem.rom = m_window;
	m_mem.rom_size = sizeof m_window;
	m_mem.ram = nullptr;            // no arena until an image claims the mailbox
	m_mem.ram_base = FN_ARENA_BASE;
	m_mem.ram_size = sizeof m_arena;

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture. Plain-Videocart
	// mode saves fine; a live mailbox across save/load is not supported.
}

// The arena exists only while the mailbox is live. A booted Videocart carries
// no claim, and a real cart with no RAM fitted leaves $8000-$FFFF undriven --
// so dropping the pointer is not a shortcut, it is the accurate model.
void channelf_fujinet_cartridge_device::set_mailbox(bool live)
{
	m_mailbox_live = live;
	m_mem.ram = live ? m_arena : nullptr;
}

void channelf_fujinet_cartridge_device::device_reset()
{
	chfmap_plan_t plan;

	// Once only: the loaded image exists by the first reset, and later resets
	// (F3) must leave the running mailbox alone. On this console that is more
	// than a convention -- the connector has no RESET pin at all, so the cart
	// cannot be reset by the console even in principle.
	if (m_init_done)
		return;
	m_init_done = true;

	if (m_rom && chfmap_plan(m_rom, m_rom_size, &plan) == CHFMAP_OK)
	{
		chfmap_apply(m_rom, &plan, m_window);
		set_mailbox(plan.mailbox_ok);
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
	// The driver maps $0800-$FFFF here, so the console address is offset plus
	// the cart base. No side_effects_disabled() guard: on this console every
	// mailbox page is write-only and a read of one is inert, so the debugger
	// cannot disturb a transaction by looking at it.
	return chf_read(&m_mem, uint16_t(offset + FN_ROM_BASE));
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
		// One store is a whole register write; the cart synthesises the
		// REGSEL/REGDATA pair the shared decoder expects.
		fujimail_read_hotspot(uint16_t(FN_H_REGSEL + reg));
		fujimail_read_hotspot(uint16_t(FN_H_REGDATA + val));
		break;

	case CHF_EV_TX:
		fujimail_read_hotspot(uint16_t(FN_H_DATA + val));
		break;

	case CHF_EV_SWAP:
		// Handled here rather than in fujimail because the swap must happen
		// inline in whatever serves the bus -- the RP2040 does it in core1's
		// loop for the same reason.
		if (m_swap_armed)
			do_swap();
		break;

	default:
		break;
	}
}

void channelf_fujinet_cartridge_device::poke(unsigned offset, uint8_t value)
{
	if (offset < sizeof m_arena)
		m_arena[offset] = value;
	// Unlike the ColecoVision there is nothing to mirror into a staged image:
	// the arena is a separate buffer from the ROM window, so it survives a swap
	// intact and a staged client boots into the status pages it was already
	// watching.
}

uint8_t channelf_fujinet_cartridge_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == FN_STREAM_ROM) ? chfmap_gate(size) : 0;

	if (err != 0)
		return err;
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
		// The .cfg sibling. The ColecoVision needs it to disambiguate mappers;
		// the Channel F has none to disambiguate, so it is accepted and dropped.
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

	chfmap_apply(v.data(), &plan, m_staged);
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
	std::memcpy(m_window, m_staged, sizeof m_window);
	m_swap_armed = false;
	m_have_staged = false;
	set_mailbox(m_staged_claims);
	if (m_mailbox_live)
		fujimail_paint();
	fprintf(stderr, "fujinet: swapped in the staged image (%u bytes); mailbox %s for this session\n",
			unsigned(m_staged_plan.size), m_mailbox_live ? "kept" : "disabled");
}
