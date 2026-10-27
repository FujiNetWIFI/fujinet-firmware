// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Emerson Arcadia 2001 FujiNet cartridge emulation

 See fujinet.h.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

// The firmware's own sources, copied in by apply.sh and built as C++ (MAME's forced
// precompiled header), so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "arcmap.h"
#include "arcapp.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(ARCADIA_ROM_FUJINET, arcadia_rom_fujinet_device, "arcadia_rom_fujinet", "Emerson Arcadia 2001 FujiNet Cart")

// fujimail's callbacks take no context argument.
static arcadia_rom_fujinet_device *s_fujinet = nullptr;

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

arcadia_rom_fujinet_device::arcadia_rom_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ARCADIA_ROM_FUJINET, tag, owner, clock)
	, device_arcadia_cart_interface(mconfig, *this)
{
}

arcadia_rom_fujinet_device::~arcadia_rom_fujinet_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void arcadia_rom_fujinet_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_bootdump = getenv("FUJINET_BOOTDUMP");
	for (auto &slot : m_slot)
		slot.assign(ARCMAP_STORE, 0xff);
	std::memset(m_ff, 0xff, sizeof m_ff);
	arcapp_reset(&m_app, m_slot[0].data(), 0, m_ff);

	// No save_item: the mailbox state is in the shared C globals, so a live mailbox does
	// not survive save/load.
}

void arcadia_rom_fujinet_device::device_reset()
{
	arcmap_plan_t plan;

	// Later resets (F3) leave the mailbox alone, as the console's RESET does the RP2040.
	if (m_init_done)
		return;
	m_init_done = true;

	plan.npages = 0;
	if (m_rom && arcmap_plan(m_rom, m_rom_size, &plan) == ARCMAP_OK)
	{
		arcmap_apply(m_rom, &plan, m_slot[0].data());
		m_mailbox_live = plan.mailbox_ok;
		if (plan.npages)
			fprintf(stderr, "fujinet: APPBANK image, %u pages; mailbox kept\n", plan.npages);
	}
	else
	{
		if (m_rom)
			fprintf(stderr, "fujinet: a %u-byte image maps to nothing on this cart\n", unsigned(m_rom_size));
		plan.npages = 0;
		m_mailbox_live = false;
	}
	m_npages[0] = plan.npages;
	arcapp_reset(&m_app, m_slot[0].data(), plan.npages, m_ff);

	if (m_mailbox_live)
	{
		fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
		fujitcp_init(nullptr);
		fujimail_paint();
	}
	else
		fprintf(stderr, "fujinet: image carries no claim signature; running with the mailbox dead\n");
}

// a14 = console address & 0x3FFF.
uint8_t arcadia_rom_fujinet_device::serve(unsigned a14)
{
	int img = arcmap_decode(a14);

	if (img < 0)
		return 0xff;                // A12 high: chip select off, open bus

	if (m_mailbox_live && (img == FN_R_TICK_LO || img == FN_R_TICK_HI))
	{
		uint16_t const tick = uint16_t(machine().time().as_ticks(60));
		return (img == FN_R_TICK_LO) ? uint8_t(tick) : uint8_t(tick >> 8);
	}

	uint8_t data = arcapp_byte(&m_app, unsigned(img));

	// The swap, bank selects and RAM writes act inline, as on core1; never for the debugger.
	if (!machine().side_effects_disabled() && img >= FN_H_REGSEL)
	{
		if (img == FN_H_REGSEL + FN_HOT_SWAP)
		{
			if (m_swap_armed)
				do_swap();
		}
		else if (m_mailbox_live && !arcapp_hotspot(&m_app, unsigned(img)))
			fujimail_read_hotspot(uint16_t(img));
	}
	return data;
}

uint8_t arcadia_rom_fujinet_device::read_rom(offs_t offset)
{
	return serve(offset & 0x0fff);          // console $0000-$0FFF: block 1
}

uint8_t arcadia_rom_fujinet_device::extra_rom(offs_t offset)
{
	// Installed over console $2000-$7FFF with offsets relative to $2000.
	return serve((offset + 0x2000) & 0x3fff);
}

void arcadia_rom_fujinet_device::poke(unsigned offset, uint8_t value)
{
	if (offset == FN_R_TICK_LO || offset == FN_R_TICK_HI)
		return;                     // the tick is time's, as on the cartridge
	m_slot[m_live][offset & 0x1fff] = value;
	// A staged claiming image gets the same publishes, or its client would boot into a
	// stale status page.
	if (m_have_staged && m_staged_claims)
		m_slot[m_live ^ 1][offset & 0x1fff] = value;
}

uint8_t arcadia_rom_fujinet_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == 0) ? arcmap_gate(size) : 0;

	if (err != 0)
		return err;
	std::vector<uint8_t> &v = m_rx[stream & 1];
	v.clear();
	if (size)
		v.reserve(size);
	return 0;
}

void arcadia_rom_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];

	v.insert(v.end(), chunk, chunk + len);
}

uint8_t arcadia_rom_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];
	arcmap_plan_t plan;

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

	if (aborted || stream != 0)
	{
		v.clear();      // the .cfg sibling means nothing to this cartridge
		return 0;
	}

	switch (arcmap_plan(v.data(), uint32_t(v.size()), &plan))
	{
	case ARCMAP_OK:
		break;
	case ARCMAP_ETOOBIG:
		v.clear();
		return FN_BOOT_ERR_TOOBIG;
	case ARCMAP_ENOMAP:
	case ARCMAP_ENOCLAIM:
		v.clear();
		return FN_BOOT_ERR_NOMAP;
	default:
		v.clear();
		return FN_BOOT_ERR_TRUNCATED;
	}
	std::vector<uint8_t> &staged = m_slot[m_live ^ 1];
	std::fill(staged.begin(), staged.end(), 0xff);
	arcmap_apply(v.data(), &plan, staged.data());
	if (plan.mailbox_ok)
		arcmap_carry_status(staged.data(), m_slot[m_live].data());
	v.clear();
	m_npages[m_live ^ 1] = plan.npages;
	m_staged_claims = plan.mailbox_ok;
	m_have_staged = true;
	return 0;
}

void arcadia_rom_fujinet_device::arm_swap()
{
	if (m_have_staged)
		m_swap_armed = true;
}

void arcadia_rom_fujinet_device::do_swap()
{
	// No repaint: the status page was carried over at staging.
	m_live ^= 1;
	arcapp_reset(&m_app, m_slot[m_live].data(), m_npages[m_live], m_ff);
	m_swap_armed = false;
	m_have_staged = false;
	m_mailbox_live = m_staged_claims;
	fprintf(stderr, "fujinet: swapped in the staged image; mailbox %s for this session\n",
			m_mailbox_live ? "kept" : "disabled");
}
