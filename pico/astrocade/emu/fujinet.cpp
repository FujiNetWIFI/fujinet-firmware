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

static void c_stream_end(int stream, const uint8_t *data, unsigned len, bool aborted)
{
	s_fujinet->stream_end(stream, data, len, aborted);
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
	c_stream_end,
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
	c_stream_end,
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

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture. Plain-ROM mode
	// saves fine; a live mailbox across save/load is not supported.
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
		astromap_apply(m_rom, &plan, m_window);
		m_mailbox_live = plan.mailbox_ok;
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
		fprintf(stderr, "fujinet: image carries no claim signature; running as a plain ROM\n");
}

uint8_t astrocade_rom_fujinet_device::read_rom(offs_t offset)
{
	offset &= 0x1fff;

	uint8_t data = m_window[offset];

	// Hotspot side effects: never for the debugger, never once the mailbox
	// is dead. The swap is handled here rather than in fujimail because it
	// must happen inline in whatever serves the bus (the RP2040 does it in
	// core1's loop for the same reason).
	if (!machine().side_effects_disabled() && m_mailbox_live && offset >= FN_H_REGSEL)
	{
		if (offset == FN_H_REGSEL + FN_HOT_SWAP)
		{
			if (m_swap_armed)
				do_swap();
		}
		else
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
	// bug in the o2 firmware port).
	if (m_have_staged && m_staged_claims)
		m_staged[offset & 0x1fff] = value;
}

void astrocade_rom_fujinet_device::stream_end(int stream, const uint8_t *data, unsigned len, bool aborted)
{
	astromap_plan_t plan;

	if (m_bootdump)
	{
		char path[512];
		snprintf(path, sizeof path, "%s%s", m_bootdump, stream ? ".cfg" : ".rom");
		FILE *f = fopen(path, "wb");
		if (f)
		{
			fwrite(data, 1, len, f);
			fclose(f);
			fprintf(stderr, "fujinet: bootdump %s (%u bytes)\n", path, len);
		}
		else
			fprintf(stderr, "fujinet: cannot write %s\n", path);
	}

	if (aborted || stream != 0 || len == 0)
		return;         // the .cfg sibling means nothing to this cartridge

	if (astromap_plan(data, len, &plan) != ASTROMAP_OK)
	{
		poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
		poke(FN_R_BOOT_ERR, FN_BOOT_ERR_NOMAP);
		return;
	}
	astromap_apply(data, &plan, m_staged);
	m_staged_claims = plan.mailbox_ok;
	m_have_staged = true;
}

void astrocade_rom_fujinet_device::arm_swap()
{
	if (m_have_staged)
		m_swap_armed = true;
}

void astrocade_rom_fujinet_device::do_swap()
{
	std::memcpy(m_window, m_staged, sizeof m_window);
	m_swap_armed = false;
	m_have_staged = false;
	m_mailbox_live = m_staged_claims;
	if (m_mailbox_live)
		fujimail_paint();
	fprintf(stderr, "fujinet: swapped in the staged image; mailbox %s for this session\n",
			m_mailbox_live ? "kept" : "disabled");
}
