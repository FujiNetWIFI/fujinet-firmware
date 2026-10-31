// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Magnavox Odyssey 2 / Philips Videopac FujiNet cartridge emulation

 See fujinet.h; pico/o2/README.md in fujinet-firmware for the bring-up.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include "video.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, copied in by apply-mame.sh.
#include "fuji_mailbox.h"
#include "fujibus.h"
#include "fujimail.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(O2_ROM_FUJINET, o2_fujinet_device, "o2_fujinet", "Odyssey 2 FujiNet Cartridge")

// fujimail's port callbacks take no context argument.
static o2_fujinet_device *s_fujinet = nullptr;

// set on the worker thread: its pokes queue for the console's thread
static thread_local bool s_on_worker = false;

// $FUJINET_PACE_MS: how long each acknowledged push frame is held
static unsigned s_pace_ms = 0;

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

static void c_send_bare(uint8_t device, uint8_t command, const uint8_t *payload, uint16_t len)
{
	if (s_pace_ms && device == FUJI_DEVICEID_DBC && command == CMD_FUJI_ACK)
		std::this_thread::sleep_for(std::chrono::milliseconds(s_pace_ms));
	fujitcp_send_bare(device, command, payload, len);
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
	c_send_bare,
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
	c_send_bare,
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
	worker_stop();
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
	const char *pace = getenv("FUJINET_PACE_MS");
	s_pace_ms = pace ? unsigned(atoi(pace)) : 0;
	std::memset(m_tbl, 0xff, sizeof m_tbl);

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture.
}

void o2_fujinet_device::device_stop()
{
	worker_stop();
}

void o2_fujinet_device::device_reset()
{
	o2map_plan_t plan;

	if (m_init_done)
		return;
	m_init_done = true;

	// The loaded image stands in for the baked-in CONFIG, so the mailbox is
	// live whatever the image claims, and it can push: an APPBANK image is
	// served from this device's own copy, not the stage buffer that
	// fujimail_stage_live guards.
	m_run = 0;
	if (m_rom && o2map_plan(m_rom.get(), m_rom_size, &plan) == O2MAP_OK)
	{
		if (plan.appbank)
		{
			m_app.assign(m_rom.get(), m_rom.get() + m_rom_size);
			o2app_init(&m_appst, m_app.data(), plan.npages);
			m_app_live = true;
			fprintf(stderr, "fujinet: APPBANK image, %u pages, as the baked CONFIG\n", plan.npages);
		}
		else
		{
			o2map_apply(m_rom.get(), &plan, m_tbl[0]);
			fprintf(stderr, "fujinet: %u-byte image as %u x %uK bank(s), as the baked CONFIG\n",
					unsigned(m_rom_size), plan.nbanks, plan.bank_bytes / 1024);
		}
	}
	else
		fprintf(stderr, "fujinet: image is not mappable; serving open bus\n");

	fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
	fujitcp_close();                    // fujinet-pc's BoIP port takes one client
	fujitcp_init(nullptr);
	fujimail_paint();

	const char *async = getenv("FUJINET_ASYNC");
	m_async = async ? atoi(async) != 0 : machine().video().throttled();
	fprintf(stderr, "fujinet: transactions run %s\n", m_async ? "on a worker thread" : "inside the console's write");
	if (m_async)
		m_worker = std::thread(&o2_fujinet_device::worker_main, this);
}

// The bus loop, picopac_cart.c core1_main: bank = 3 - (P10 + 2*P11) for a
// legacy image, o2app_read for an APPBANK one, and a staged image taking over
// on the console's fetch of the cartridge reset vector.
u8 o2_fujinet_device::fetch(unsigned addr)
{
	if (m_async)
		drain_pokes();
	if (m_boot_armed.load(std::memory_order_acquire) && addr == 0x400 && !machine().side_effects_disabled())
		take_staged();

	if (m_app_live)
		return o2app_read(&m_appst, addr, m_tbl[m_run][0], fujimail_rx, fujimail_rxn);
	return m_tbl[m_run][3 - ((m_p1 & 1) + (m_p1 & 2))][addr];
}

// fuji_cart_swap(): the staged image becomes the running one. The cart gives a
// claiming image the running status bytes by poking both tables; here they
// are copied across at the swap, which the console cannot tell apart.
void o2_fujinet_device::take_staged()
{
	std::lock_guard<std::mutex> hold(m_loadlock);

	if (!m_boot_armed.load())
		return;
	const unsigned stage = m_run ^ 1;
	if (m_stage_mailbox)
	{
		for (auto &bank : m_tbl[stage])
			std::memcpy(&bank[FN_R_ACKSEQ], &m_tbl[m_run][0][FN_R_ACKSEQ], 0x1000 - FN_R_ACKSEQ);
	}
	if (m_next_is_app)
	{
		m_app.swap(m_app_next);
		std::vector<u8>().swap(m_app_next);
		o2app_init(&m_appst, m_app.data(), m_app_next_pages);
		m_app_live = true;
	}
	else
		m_app_live = false;
	m_mailbox = m_stage_mailbox;
	m_run = stage;
	m_boot_armed.store(false);
	fprintf(stderr, "fujinet: swapped in the staged image (%s); mailbox %s\n",
			m_next_is_app ? "APPBANK" : "legacy", m_mailbox ? "kept" : "off");
}

// Console MOVX writes, qualified the way the cartridge qualifies them: P14 low
// is its chip select (the driver has already dropped writes in copy mode).
void o2_fujinet_device::io_write(offs_t offset, u8 data)
{
	if ((m_p1 & 0x10) || offset < FN_W_REGSEL || offset > FN_W_SEQ || !m_mailbox)
		return;
	if (m_app_live && o2app_write(&m_appst, u8(offset), data))
		return;
	if (!m_async)
	{
		fujimail_write(u8(offset), data);
		return;
	}
	{
		std::lock_guard<std::mutex> hold(m_qlock);
		m_writes.emplace_back(u8(offset), data);
	}
	m_qcv.notify_one();
}

// Through fujimail's port. On the worker thread the byte is queued for the
// console's thread, which owns the tables.
void o2_fujinet_device::poke(unsigned addr, u8 value)
{
	if (!s_on_worker)
	{
		apply_poke(addr, value);
		return;
	}
	std::lock_guard<std::mutex> hold(m_pklock);
	m_pokes.emplace_back(u16(addr), value);
	m_poked.store(true, std::memory_order_release);
}

// The running image's table only: a staged game that does not claim the
// mailbox has its own bytes at $F20, and one that does gets the page at the swap.
void o2_fujinet_device::apply_poke(unsigned addr, u8 value)
{
	if (!m_mailbox)
		return;
	for (auto &bank : m_tbl[m_run])
		bank[addr & 0xfff] = value;
}

void o2_fujinet_device::drain_pokes()
{
	if (!m_poked.load(std::memory_order_acquire))
		return;

	std::vector<std::pair<u16, u8>> pokes;
	{
		std::lock_guard<std::mutex> hold(m_pklock);
		pokes.swap(m_pokes);
		m_poked.store(false, std::memory_order_relaxed);
	}
	for (const auto &entry : pokes)
		apply_poke(entry.first, entry.second);
}

void o2_fujinet_device::worker_main()
{
	s_on_worker = true;
	for (;;)
	{
		std::pair<u8, u8> write;
		{
			std::unique_lock<std::mutex> hold(m_qlock);
			m_qcv.wait(hold, [this] { return m_stop || !m_writes.empty(); });
			if (m_stop)
				return;
			write = m_writes.front();
			m_writes.pop_front();
		}
		// writes queued before a swap are the old image's
		if (m_mailbox)
			fujimail_write(write.first, write.second);
	}
}

// A transaction in flight is cut short, so a reset or exit never waits out its
// timeout.
void o2_fujinet_device::worker_stop()
{
	if (!m_worker.joinable())
		return;
	{
		std::lock_guard<std::mutex> hold(m_qlock);
		m_stop = true;
	}
	m_qcv.notify_one();
	fujitcp_abort();
	m_worker.join();
	m_writes.clear();
	m_stop = false;
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
	{
		std::lock_guard<std::mutex> hold(m_loadlock);
		if (plan.appbank)
		{
			m_app_next.assign(data, data + len);
			m_app_next_pages = plan.npages;
			m_next_is_app = true;
			fujimail_stage_live = true;
			m_stage_mailbox = true;
		}
		else
		{
			// the table the console is not running
			o2map_apply(data, &plan, m_tbl[m_run ^ 1]);
			m_next_is_app = false;
			m_stage_mailbox = plan.mailbox_ok;
		}
		m_boot_armed.store(true, std::memory_order_release);
	}
	fprintf(stderr, "fujinet: staged %u bytes (%s, mailbox %s); takes over at the next reset\n",
			len, plan.appbank ? "APPBANK" : "legacy", m_stage_mailbox ? "claimed" : "not claimed");
}
