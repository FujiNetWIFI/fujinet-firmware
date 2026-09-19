// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Bally Astrocade FujiNet cartridge emulation

 See fujinet.h for the design; pico/astrocade/README.md in fujinet-firmware
 for the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own protocol sources, compiled in verbatim
// (copied next to this file by pico/astrocade/emu/apply.sh -- as C++, to
// live with MAME's forced C++ precompiled header, so no extern "C").
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "astromap.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(ASTROCADE_ROM_FUJINET, astrocade_rom_fujinet_device, "astrocade_rom_fujinet", "Bally Astrocade FujiNet Cart")

// fujimail's port interface is C function pointers with no context argument,
// so the single device instance is reached through this. One cart slot, one
// cart: the constraint is real hardware's too.
static astrocade_rom_fujinet_device *s_fujinet = nullptr;

static const char *kind_name(astromap_kind_t kind)
{
	switch (kind)
	{
	case ASTROMAP_FLAT:    return "flat";
	case ASTROMAP_GAME256: return "256K game mapper";
	case ASTROMAP_GAME512: return "512K game mapper";
	case ASTROMAP_APPBANK: return "banked app";
	default:               return "?";
	}
}

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

astrocade_rom_fujinet_device::astrocade_rom_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ASTROCADE_ROM_FUJINET, tag, owner, clock)
	, device_astrocade_cart_interface(mconfig, *this)
{
}

astrocade_rom_fujinet_device::~astrocade_rom_fujinet_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void astrocade_rom_fujinet_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_bootdump = getenv("FUJINET_BOOTDUMP");
	std::memset(m_window, 0xff, sizeof m_window);
	std::memset(m_staged, 0xff, sizeof m_staged);
	m_bank[0] = m_window;
	m_bank[1] = m_window + 0x1000;

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture. Plain-ROM mode
	// saves fine; a live mailbox across save/load is not supported.
}

// Point the serve state at the live image, the emulator's copy of core1's
// swap: m_image must already hold the full image for the banked kinds, and
// m_window the (first) 8K.
void astrocade_rom_fujinet_device::apply_serving(const astromap_plan_t &plan)
{
	astromap_serve_t s;

	astromap_serve_reset(&plan, &s);
	switch (plan.kind)
	{
	case ASTROMAP_APPBANK:
		m_bank[0] = m_image.data() + s.bank_off[0];
		// The high half serves from the RAM window so mailbox repaints stay
		// visible; the low half banks out of the image store.
		m_bank[1] = m_window + 0x1000;
		m_hot_base = ASTROMAP_HOT_OFF;
		m_hot_mask = 0;
		m_hot_image = nullptr;
		m_app_store = m_image.data();
		m_app_npages = plan.npages;
		break;
	case ASTROMAP_GAME256:
	case ASTROMAP_GAME512:
		m_bank[0] = m_image.data() + s.bank_off[0];
		m_bank[1] = m_image.data() + s.bank_off[1];
		m_hot_base = s.hot_base;
		m_hot_mask = s.hot_mask;
		m_hot_image = m_image.data();
		m_app_store = nullptr;
		m_app_npages = 0;
		break;
	default:
		m_bank[0] = m_window;
		m_bank[1] = m_window + 0x1000;
		m_hot_base = ASTROMAP_HOT_OFF;
		m_hot_mask = 0;
		m_hot_image = nullptr;
		m_app_store = nullptr;
		m_app_npages = 0;
		break;
	}
}

void astrocade_rom_fujinet_device::device_reset()
{
	astromap_plan_t plan;

	// Once only: the loaded image exists by the first reset, and later
	// resets (F3) must leave the running mailbox alone, exactly as the real
	// console's RESET leaves the RP2040 alone.
	if (m_init_done)
		return;
	m_init_done = true;

	if (m_rom && astromap_plan(m_rom, m_rom_size, &plan) == ASTROMAP_OK)
	{
		if (plan.kind == ASTROMAP_FLAT)
			astromap_apply(m_rom, &plan, m_window);
		else
		{
			m_image.assign(m_rom, m_rom + m_rom_size);
			std::memcpy(m_window, m_rom, sizeof m_window);
		}
		apply_serving(plan);
		m_mailbox_live = plan.mailbox_ok;
		if (plan.kind != ASTROMAP_FLAT)
			fprintf(stderr, "fujinet: %u-byte image loaded as %s\n",
					unsigned(m_rom_size), kind_name(plan.kind));
	}
	else
		m_mailbox_live = false;

	if (m_mailbox_live)
	{
		fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
		fujitcp_init(nullptr);
		fujimail_paint();
	}
	else
		fprintf(stderr, "fujinet: image carries no claim signature; running with the mailbox dead\n");
}

uint8_t astrocade_rom_fujinet_device::read_rom(offs_t offset)
{
	offset &= 0x1fff;

	if (offset >= m_hot_base)
	{
		// Game bank hotspot: the read RETURNS the new bank number, exactly
		// as rom_256k/rom_512k. The debugger sees the value but must not
		// switch (stricter than MAME's own mappers, which commit).
		uint8_t data = offset & m_hot_mask;
		if (!machine().side_effects_disabled())
			m_bank[1] = m_hot_image + (uint32_t(data) << 12);
		return data;
	}

	uint8_t data = m_bank[offset >> 12][offset & 0xfff];

	// Hotspot side effects: never for the debugger, never once the mailbox
	// is dead. The swap and the bank selects are handled here rather than in
	// fujimail because they must happen inline in whatever serves the bus
	// (the RP2040 does both in core1's loop for the same reason).
	if (!machine().side_effects_disabled() && offset >= FN_H_REGSEL)
	{
		if (offset == FN_H_REGSEL + FN_HOT_SWAP)
		{
			if (m_swap_armed)
				do_swap();
		}
		else if ((offset & FN_H_PAGE_MASK) == FN_H_REGSEL
				 && (offset & 0xff) >= FN_HOT_BANK)
		{
			unsigned page = (offset & 0xff) - FN_HOT_BANK;
			if (page < m_app_npages)
				m_bank[0] = m_app_store + (uint32_t(page) << 12);
		}
		else if (m_mailbox_live)
			fujimail_read_hotspot(uint16_t(offset));
	}
	return data;
}

void astrocade_rom_fujinet_device::poke(unsigned offset, uint8_t value)
{
	m_window[offset & 0x1fff] = value;
	// A staged image that claims the mailbox must receive the same
	// publishes, or the client would boot into stale status pages. One that
	// does not claim it keeps its bytes pristine -- and the mailbox stays
	// live on the *current* window until the swap actually happens, so the
	// client still sees BOOT_READY (deactivating at stage time is a latent
	// bug in the o2 firmware port). Every mailbox offset is >= 0x1B00, the
	// high half, which an APPBANK image always serves from m_window.
	if (m_have_staged && m_staged_claims)
		m_staged[offset & 0x1fff] = value;
}

uint8_t astrocade_rom_fujinet_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == 0) ? astromap_gate(size) : 0;

	if (err != 0)
		return err;
	std::vector<uint8_t> &v = m_rx[stream & 1];
	v.clear();
	if (size)
		v.reserve(size);
	return 0;
}

void astrocade_rom_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];

	v.insert(v.end(), chunk, chunk + len);
}

uint8_t astrocade_rom_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];
	astromap_plan_t plan;

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

	if (v.empty() || astromap_plan(v.data(), uint32_t(v.size()), &plan) != ASTROMAP_OK)
	{
		v.clear();
		return FN_BOOT_ERR_NOMAP;
	}
	if (plan.kind == ASTROMAP_FLAT)
	{
		astromap_apply(v.data(), &plan, m_staged);
		m_staged_image.clear();
		v.clear();
	}
	else
	{
		m_staged_image = std::move(v);
		std::memcpy(m_staged, m_staged_image.data(), sizeof m_staged);
	}
	m_staged_plan = plan;
	m_staged_claims = plan.mailbox_ok;
	m_have_staged = true;
	return 0;
}

void astrocade_rom_fujinet_device::arm_swap()
{
	if (m_have_staged)
		m_swap_armed = true;
}

void astrocade_rom_fujinet_device::do_swap()
{
	std::memcpy(m_window, m_staged, sizeof m_window);
	m_image = std::move(m_staged_image);
	apply_serving(m_staged_plan);
	m_swap_armed = false;
	m_have_staged = false;
	m_mailbox_live = m_staged_claims;
	if (m_mailbox_live)
		fujimail_paint();
	fprintf(stderr, "fujinet: swapped in the staged image (%s); mailbox %s for this session\n",
			kind_name(m_staged_plan.kind), m_mailbox_live ? "kept" : "disabled");
}
