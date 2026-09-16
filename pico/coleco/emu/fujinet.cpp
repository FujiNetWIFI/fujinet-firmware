// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 ColecoVision FujiNet cartridge emulation

 See fujinet.h for the design; pico/coleco/README.md in fujinet-firmware for
 the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own protocol sources, compiled in verbatim (copied
// next to this file by pico/coleco/emu/apply.sh -- as C++, to live with MAME's
// forced C++ precompiled header, so no extern "C").
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "colmap.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(COLECOVISION_FUJINET, colecovision_fujinet_cartridge_device, "colecovision_fujinet", "ColecoVision FujiNet Cart")

// fujimail's port interface is C function pointers with no context argument,
// so the single device instance is reached through this. One cart slot, one
// cart: the constraint is real hardware's too.
static colecovision_fujinet_cartridge_device *s_fujinet = nullptr;

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

colecovision_fujinet_cartridge_device::colecovision_fujinet_cartridge_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, COLECOVISION_FUJINET, tag, owner, clock)
	, device_colecovision_cartridge_interface(mconfig, *this)
{
}

colecovision_fujinet_cartridge_device::~colecovision_fujinet_cartridge_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void colecovision_fujinet_cartridge_device::device_start()
{
	colmap_plan_t flat{ COLMAP_WINDOW, COLMAP_FLAT, false, 0 };

	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_bootdump = getenv("FUJINET_BOOTDUMP");
	std::memset(m_window, 0xff, sizeof m_window);
	std::memset(m_staged, 0xff, sizeof m_staged);
	colmap_serve_reset(&flat, &m_serve);
	m_base = m_window;

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture. Plain-ROM mode
	// saves fine; a live mailbox across save/load is not supported.
}

// Point the serve state at the live image, the emulator's copy of core1's
// swap: m_image must already hold the full image for the banked kinds, and
// m_window the flat one.
void colecovision_fujinet_cartridge_device::apply_serving(const colmap_plan_t &plan)
{
	colmap_serve_reset(&plan, &m_serve);
	m_base = colmap_serves_window(&plan) ? m_window : m_image.data();
}

void colecovision_fujinet_cartridge_device::device_reset()
{
	colmap_plan_t plan;

	// Once only: the loaded image exists by the first reset, and later resets
	// (F3) must leave the running mailbox alone, exactly as the real console's
	// RESET leaves the RP2040 alone.
	if (m_init_done)
		return;
	m_init_done = true;

	if (m_rom && colmap_plan(m_rom, m_rom_size, COLMAP_KIND_AUTO, &plan) == COLMAP_OK)
	{
		if (colmap_serves_window(&plan))
			colmap_apply(m_rom, &plan, m_window);
		else
			m_image.assign(m_rom, m_rom + m_rom_size);
		apply_serving(plan);
		m_mailbox_live = plan.mailbox_ok;
		if (!colmap_serves_window(&plan))
			fprintf(stderr, "fujinet: %u-byte image loaded as %s\n",
					unsigned(m_rom_size), colmap_kindname(plan.kind));
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

uint8_t colecovision_fujinet_cartridge_device::read(offs_t offset, int _8000, int _a000, int _c000, int _e000)
{
	// coleco.cpp passes every select asserted (cart_r hands us four zeroes),
	// so the block comes from the address, not from these. Kept in the
	// signature because the interface has them.
	(void)_8000; (void)_a000; (void)_c000; (void)_e000;

	offset &= (COLMAP_WINDOW - 1);

	const bool commit = !machine().side_effects_disabled();

	// A mapper that stops driving here reads back as an undriven bus. This is
	// stricter than MAME's own devices -- sgc.cpp happily returns flash data
	// from $FFFC -- and it is the point: the soak should measure what the
	// silicon does, not what the emulator finds convenient.
	if (colmap_tristate(&m_serve, uint16_t(offset)))
		return 0xff;

	int32_t addr = colmap_serve(&m_serve, uint16_t(offset), commit);
	uint8_t data = (addr < 0) ? 0xff : m_base[addr];

	// Hotspot side effects: never for the debugger, never once the mailbox is
	// dead. The swap is handled here rather than in fujimail because it must
	// happen inline in whatever serves the bus -- the RP2040 does it in
	// core1's loop for the same reason.
	if (commit && offset >= FN_H_REGSEL)
	{
		if (offset == FN_H_REGSEL + FN_HOT_SWAP)
		{
			if (m_swap_armed)
				do_swap();
		}
		else if (m_mailbox_live)
			fujimail_read_hotspot(uint16_t(offset));
	}
	return data;
}

void colecovision_fujinet_cartridge_device::write(offs_t offset, uint8_t data, int _8000, int _a000, int _c000, int _e000)
{
	(void)_8000; (void)_a000; (void)_c000; (void)_e000;

	offset &= (COLMAP_WINDOW - 1);

	// On the real port a write is indistinguishable from a read, so anything a
	// read would have done, a write does too -- that is exactly how Activision
	// carts switch banks. The data byte only matters to the SGC.
	colmap_serve_write(&m_serve, uint16_t(offset), data);
	if (!machine().side_effects_disabled())
		(void)colmap_serve(&m_serve, uint16_t(offset), true);
}

void colecovision_fujinet_cartridge_device::poke(unsigned offset, uint8_t value)
{
	m_window[offset & (COLMAP_WINDOW - 1)] = value;
	// A staged image that claims the mailbox must receive the same publishes,
	// or the client would boot into stale status pages. One that does not
	// claim it keeps its bytes pristine -- and the mailbox stays live on the
	// *current* window until the swap actually happens, so the client still
	// sees BOOT_READY (deactivating at stage time is a latent bug in the o2
	// firmware port).
	if (m_have_staged && m_staged_claims)
		m_staged[offset & (COLMAP_WINDOW - 1)] = value;
}

uint8_t colecovision_fujinet_cartridge_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == FN_STREAM_ROM) ? colmap_gate(size) : 0;

	if (err != 0)
		return err;
	std::vector<uint8_t> &v = m_rx[stream & 1];
	v.clear();
	if (size)
		v.reserve(size);
	return 0;
}

void colecovision_fujinet_cartridge_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];

	v.insert(v.end(), chunk, chunk + len);
}

uint8_t colecovision_fujinet_cartridge_device::stream_close(int stream, uint32_t got, bool aborted)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];
	colmap_plan_t plan;
	colmap_kind_t hint;

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

	if (stream == FN_STREAM_CFG)
	{
		// The mapper hint. Three Activision carts are 64K and two Opcode SGC
		// carts are 128K, and both sizes are also MegaCart sizes -- nothing in
		// the image can tell them apart, so a one-line .cfg has to.
		m_pending_hint = (aborted || v.empty())
			? COLMAP_KIND_AUTO
			: colmap_parse_cfg(reinterpret_cast<const char *>(v.data()), uint32_t(v.size()));
		v.clear();
		return 0;
	}
	if (stream != FN_STREAM_ROM)
	{
		v.clear();
		return 0;
	}

	// Consume the hint here whatever happens next: the ESP32 sends a .cfg only
	// when the file exists, so a mount with no sibling sends nothing at all and
	// would otherwise inherit the previous mount's mapper.
	hint = m_pending_hint;
	m_pending_hint = COLMAP_KIND_AUTO;

	if (aborted)
	{
		v.clear();
		return 0;
	}

	if (v.empty() || colmap_plan(v.data(), uint32_t(v.size()), hint, &plan) != COLMAP_OK)
	{
		v.clear();
		return FN_BOOT_ERR_NOMAP;
	}
	if (colmap_serves_window(&plan))
	{
		colmap_apply(v.data(), &plan, m_staged);
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

void colecovision_fujinet_cartridge_device::arm_swap()
{
	if (m_have_staged)
		m_swap_armed = true;
}

void colecovision_fujinet_cartridge_device::do_swap()
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
			colmap_kindname(m_staged_plan.kind), m_mailbox_live ? "kept" : "disabled");
}
