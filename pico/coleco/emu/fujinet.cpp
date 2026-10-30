// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 ColecoVision FujiNet cartridge emulation; see fujinet.h.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge's own sources, grafted in as C++ by pico/coleco/emu/apply.sh.
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "colmap.h"
#include "fuji_cfg.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(COLECOVISION_FUJINET, colecovision_fujinet_cartridge_device, "colecovision_fujinet", "ColecoVision FujiNet Cart")

// fujimail's port callbacks take no context, so they reach the one device through this.
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

static void c_on_txn(const fujimail_txn_t *txn)
{
	char txt[40];
	unsigned idx, len = 0;

	for (idx = 0; idx < txn->rxlen && len < sizeof txt - 1; idx++)
	{
		uint8_t chr = txn->rx[idx];
		if (chr == 0)
			break;
		txt[len++] = (chr >= 0x20 && chr < 0x7F) ? char(chr) : '.';
	}
	txt[len] = '\0';
	fprintf(stderr,
			"fujinet: dev=%02X cmd=%02X nparam=%u txlen=%u seq=%u"
			" -> err=%d reply=%02X rxlen=%u%s%s%s\n",
			txn->device, txn->command, txn->nparam, txn->txlen, txn->seq,
			txn->status, txn->reply_cmd, txn->rxlen,
			len ? " \"" : "", txt, len ? "\"" : "");
}

static void c_on_dbc(fujimail_dbc_ev_t event, int stream, uint32_t expect, unsigned got, bool aborted)
{
	if (event == FUJIMAIL_DBC_OPEN)
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

	// No save state for a live mailbox: its state is in fujimail's globals.
}

// Serve the live image: m_image holds a banked one, m_window a flat one.
void colecovision_fujinet_cartridge_device::apply_serving(const colmap_plan_t &plan)
{
	colmap_serve_reset(&plan, &m_serve);
	m_base = colmap_serves_window(&plan) ? m_window : m_image.data();
}

void colecovision_fujinet_cartridge_device::device_reset()
{
	colmap_plan_t plan;

	// Once only: the console's RESET never reaches the cart.
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
	// coleco.cpp passes every select asserted; the block comes from the address.
	(void)_8000; (void)_a000; (void)_c000; (void)_e000;

	offset &= (COLMAP_WINDOW - 1);

	const bool commit = !machine().side_effects_disabled();

	// Where the cart stops driving, read an undriven bus, as the silicon would; stricter
	// than sgc.cpp, which returns flash data from $FFFC.
	if (colmap_tristate(&m_serve, uint16_t(offset)))
		return 0xff;

	int32_t addr = colmap_serve(&m_serve, uint16_t(offset), commit);
	uint8_t data = (addr < 0) ? 0xff : m_base[addr];

	// Hotspots act only outside the debugger and while the mailbox is live. The swap is here,
	// inline with the read, as it is in core1's loop.
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

	// On the real port a write looks like a read, so it does what a read would (Activision
	// banks switch this way); only the SGC uses the data byte.
	colmap_serve_write(&m_serve, uint16_t(offset), data);
	if (!machine().side_effects_disabled())
		(void)colmap_serve(&m_serve, uint16_t(offset), true);
}

void colecovision_fujinet_cartridge_device::poke(unsigned offset, uint8_t value)
{
	m_window[offset & (COLMAP_WINDOW - 1)] = value;
	// A staged claiming image gets the same publishes; any other stays pristine, and the
	// mailbox stays live until the swap so the client sees BOOT_READY.
	if (m_have_staged && m_staged_claims)
		m_staged[offset & (COLMAP_WINDOW - 1)] = value;
}

uint8_t colecovision_fujinet_cartridge_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == FN_STREAM_ROM) ? colmap_gate(size) : 0;

	if (err != 0)
		return err;
	std::vector<uint8_t> &buf = m_rx[stream & 1];
	buf.clear();
	if (size)
		buf.reserve(size);
	return 0;
}

void colecovision_fujinet_cartridge_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	std::vector<uint8_t> &buf = m_rx[stream & 1];

	buf.insert(buf.end(), chunk, chunk + len);
}

uint8_t colecovision_fujinet_cartridge_device::stream_close(int stream, uint32_t got, bool aborted)
{
	std::vector<uint8_t> &buf = m_rx[stream & 1];
	colmap_plan_t plan;
	colmap_kind_t hint;

	(void) got;         // buf.size() is the byte count that actually landed

	if (m_bootdump && !buf.empty())
	{
		char path[512];
		snprintf(path, sizeof path, "%s%s", m_bootdump, stream ? ".cfg" : ".rom");
		FILE *dump = fopen(path, "wb");
		if (dump)
		{
			fwrite(buf.data(), 1, buf.size(), dump);
			fclose(dump);
			fprintf(stderr, "fujinet: bootdump %s (%u bytes)\n", path, unsigned(buf.size()));
		}
		else
			fprintf(stderr, "fujinet: cannot write %s\n", path);
	}

	if (stream == FN_STREAM_CFG)
	{
		// The .cfg's mapper= tells Activision and SGC carts from MegaCarts of the same size.
		fuji_cfg_t cfg;

		fuji_cfg_open(&cfg);
		fuji_cfg_write(&cfg, buf.data(), unsigned(buf.size()));
		fuji_cfg_close(&cfg, aborted);
		m_pending_hint = colmap_kind_named(fuji_cfg_mapper(&cfg));
		buf.clear();
		return 0;
	}
	if (stream != FN_STREAM_ROM)
	{
		buf.clear();
		return 0;
	}

	// Use the hint up: an image with no .cfg must not inherit the last one's mapper.
	hint = m_pending_hint;
	m_pending_hint = COLMAP_KIND_AUTO;

	if (aborted)
	{
		buf.clear();
		return 0;
	}

	if (buf.empty() || colmap_plan(buf.data(), uint32_t(buf.size()), hint, &plan) != COLMAP_OK)
	{
		buf.clear();
		return FN_BOOT_ERR_NOMAP;
	}
	if (colmap_serves_window(&plan))
	{
		colmap_apply(buf.data(), &plan, m_staged);
		m_staged_image.clear();
		buf.clear();
	}
	else
	{
		m_staged_image = std::move(buf);
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
