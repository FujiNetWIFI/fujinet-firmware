// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Magnavox Odyssey 2 / Philips Videopac FujiNet cartridge emulation

 The pico/o2 cartridge for MAME, running the cartridge's own fujimail.c,
 o2map.c and o2app.h, and reaching a fujinet-pc over TCP ($FUJINET_TCP,
 default 127.0.0.1:9995).

 MAME's i8244 is the stricter model for a client that rewrites the VDC
 mid-frame: it refuses object register writes while the foreground is on,
 draws per scanline, and runs 22.75 CPU cycles to the line.

   mame odyssey2 -cartslot fujinet -cart image.bin

 What it serves at power-on is the baked CONFIG: the -cart image, a legacy one
 or an APPBANK (protocol 2) one, with the mailbox live whatever it claims. The
 image the client mounts is staged beside it and takes over, as on the cart,
 at the console's next fetch of $400, which is what the BIOS does after a reset
 (the client's `jmp $000`). Until then the CONFIG keeps running and keeps its
 status page, so the reply to the MOUNT_IMAGE that staged the image reaches it;
 a staged game that does not claim the mailbox never has one written into it.

 Throttled, transactions run on a worker thread, as on the cart's core0, so a
 client polling the status page sees a push's percent climb. Under -nothrottle
 they run inside the console's write, so emulated time cannot outrun a network
 wait. $FUJINET_ASYNC=0/1 overrides. $FUJINET_PACE_MS=n holds each acknowledged
 push frame n ms, to make a localhost push last long enough to watch.

 ***********************************************************************************************************/

#ifndef MAME_BUS_ODYSSEY2_FUJINET_H
#define MAME_BUS_ODYSSEY2_FUJINET_H

#pragma once

#include "slot.h"
#include "o2app.h"
#include "o2map.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>


class o2_fujinet_device : public device_t, public device_o2_cart_interface
{
public:
	o2_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
	virtual ~o2_fujinet_device();

	virtual u8 read_rom04(offs_t offset) override { return fetch(0x400 + offset); }
	virtual u8 read_rom0c(offs_t offset) override { return fetch(0xc00 + offset); }
	virtual void write_p1(u8 data) override { m_p1 = data; }
	virtual void io_write(offs_t offset, u8 data) override;

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned addr, u8 value);
	void stream_end(int stream, const u8 *data, unsigned len, bool aborted);

protected:
	virtual void device_start() override ATTR_COLD;
	// The image loads after device_start, so the cart comes up here, once:
	// the console's RESET does not reach the cartridge connector.
	virtual void device_reset() override;
	virtual void device_stop() override ATTR_COLD;

private:
	u8 fetch(unsigned addr);
	void take_staged();
	void apply_poke(unsigned addr, u8 value);
	void drain_pokes();
	void worker_main();
	void worker_stop();

	// Two tables of legacy images (or, for an APPBANK image, just its mailbox
	// bytes): m_tbl[m_run] is the one served, the other is where a push lands.
	u8 m_tbl[2][O2MAP_BANKS][O2MAP_BANK_BYTES];
	unsigned m_run = 0;

	// Protocol 2: the APPBANK image served through o2app.h, and the staged one.
	std::vector<u8> m_app;
	std::vector<u8> m_app_next;
	unsigned m_app_next_pages = 0;
	o2app_t m_appst{};

	u8 m_p1 = 0x13;             // P10, P11, P14 as the driver passes them
	bool m_init_done = false;
	bool m_app_live = false;
	std::atomic<bool> m_mailbox{true};      // the running image owns the mailbox page
	bool m_debug = false;

	// A staged image, built by whichever thread runs the transaction and taken
	// by the console's thread. m_loadlock covers it and the swap.
	std::mutex m_loadlock;
	bool m_next_is_app = false;
	std::atomic<bool> m_boot_armed{false};
	bool m_stage_mailbox = false;               // the staged image claims the mailbox

	// The worker owns fujimail and the socket; the console's thread owns the
	// tables, the swap and the app state. The worker's pokes reach the tables
	// through m_pokes, applied before the next fetch; the console's register
	// writes reach the worker through m_writes.
	bool m_async = false;
	std::thread m_worker;
	std::mutex m_qlock;                         // m_writes, m_stop
	std::condition_variable m_qcv;
	std::deque<std::pair<u8, u8>> m_writes;
	bool m_stop = false;
	std::mutex m_pklock;                        // m_pokes
	std::vector<std::pair<u16, u8>> m_pokes;
	std::atomic<bool> m_poked{false};
};

DECLARE_DEVICE_TYPE(O2_ROM_FUJINET, o2_fujinet_device)

#endif // MAME_BUS_ODYSSEY2_FUJINET_H
