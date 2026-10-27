// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Magnavox Odyssey 2 / Philips Videopac FujiNet cartridge emulation

 See fujinet.h; pico/o2/README.md in fujinet-firmware for the bring-up.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, copied in by apply-mame.sh.
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(O2_ROM_FUJINET, o2_fujinet_device, "o2_fujinet", "Odyssey 2 FujiNet Cartridge")

// fujimail's port callbacks take no context argument.
static o2_fujinet_device *s_fujinet = nullptr;

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned addr, uint8_t value)
{
	s_fujinet->poke(addr, value);
}

static bool c_link_up()
{
	return fujitcp_active();
}

static void c_stream_end(int stream, const uint8_t *data, unsigned len, bool aborted)
{
	s_fujinet->stream_end(stream, data, len, aborted);
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
		txt[len++] = (chr >= 0x20 && chr < 0x7f) ? char(chr) : '.';
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
	c_stream_end,
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
	nullptr,
	nullptr,
	nullptr,
	nullptr,
};

/*-------------------------------------------------
    device
-------------------------------------------------*/

o2_fujinet_device::o2_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, O2_ROM_FUJINET, tag, owner, clock)
	, device_o2_cart_interface(mconfig, *this)
{
}

o2_fujinet_device::~o2_fujinet_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void o2_fujinet_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	std::memset(m_banks, 0xff, sizeof m_banks);
	std::memset(m_staged, 0xff, sizeof m_staged);

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture.
}

void o2_fujinet_device::device_reset()
{
	o2map_plan_t plan;

	if (m_init_done)
		return;
	m_init_done = true;

	// The loaded image stands in for the baked-in client, so the mailbox is
	// live whatever the image claims.
	if (m_rom && o2map_plan(m_rom.get(), m_rom_size, &plan) == O2MAP_OK)
	{
		if (plan.appbank)
		{
			m_app.assign(m_rom.get(), m_rom.get() + m_rom_size);
			o2app_init(&m_appst, m_app.data(), plan.npages);
			m_app_live = true;
			fujimail_stage_live = true;
			fprintf(stderr, "fujinet: APPBANK image, %u pages\n", plan.npages);
		}
		else
		{
			o2map_apply(m_rom.get(), &plan, m_banks);
			fprintf(stderr, "fujinet: %u-byte image as %u x %uK bank(s)\n",
					unsigned(m_rom_size), plan.nbanks, plan.bank_bytes / 1024);
		}
	}
	else
		fprintf(stderr, "fujinet: image is not mappable; serving open bus\n");

	fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
	fujitcp_init(nullptr);
	fujimail_paint();
}

// The bus loop, picopac_cart.c core1_main: bank = 3 - (P10 + 2*P11) for a
// legacy image, o2app_read for an APPBANK one, and a staged image taking over
// on the console's fetch of the cartridge reset vector.
u8 o2_fujinet_device::fetch(unsigned addr)
{
	if (m_boot_armed && addr == 0x400 && !machine().side_effects_disabled())
		take_staged();

	if (m_app_live)
		return o2app_read(&m_appst, addr, m_banks[0], fujimail_rx, fujimail_rxn);
	return m_banks[3 - ((m_p1 & 1) + (m_p1 & 2))][addr];
}

void o2_fujinet_device::take_staged()
{
	if (m_next_is_app)
	{
		m_app.swap(m_app_next);
		o2app_init(&m_appst, m_app.data(), m_app_next_pages);
		m_app_live = true;
	}
	else
	{
		std::memcpy(m_banks, m_staged, sizeof m_banks);
		m_app_live = false;
	}
	m_boot_armed = false;
}

// Console MOVX writes, qualified the way the cartridge qualifies them: P14 low
// is its chip select (the driver has already dropped writes in copy mode).
void o2_fujinet_device::io_write(offs_t offset, u8 data)
{
	if ((m_p1 & 0x10) || offset < FN_W_REGSEL || offset > FN_W_SEQ || !m_mailbox)
		return;
	if (m_app_live && o2app_write(&m_appst, u8(offset), data))
		return;
	fujimail_write(u8(offset), data);
}

void o2_fujinet_device::poke(unsigned addr, u8 value)
{
	if (!m_mailbox)
		return;
	for (auto &bank : m_banks)
		bank[addr & 0xfff] = value;
	if (m_publish_staged)
		for (auto &bank : m_staged)
			bank[addr & 0xfff] = value;
}

// fujinet.c's port_stream_end, with the swap left to fetch().
void o2_fujinet_device::stream_end(int stream, const u8 *data, unsigned len, bool aborted)
{
	o2map_plan_t plan;

	if (aborted || stream != 0 || len == 0)
		return;
	if (o2map_plan(data, len, &plan) != O2MAP_OK)
	{
		poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
		poke(FN_R_BOOT_ERR, FN_BOOT_ERR_NOMAP);
		return;
	}
	if (plan.appbank)
	{
		m_app_next.assign(data, data + len);
		m_app_next_pages = plan.npages;
		m_next_is_app = true;
		fujimail_stage_live = true;
		m_mailbox = true;
	}
	else
	{
		o2map_apply(data, &plan, m_staged);
		m_next_is_app = false;
		m_mailbox = plan.mailbox_ok;
	}
	m_publish_staged = true;
	m_boot_armed = true;
	if (m_mailbox)
		fujimail_paint();
	fprintf(stderr, "fujinet: staged %u bytes; takes over at the next reset\n", len);
}
