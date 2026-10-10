// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_RCA_STUDIO2_FUJINET_H
#define MAME_RCA_STUDIO2_FUJINET_H

#pragma once

#include "s2map.h"
#include "s2_cart.h"
#include "s2_text.h"
#include "fuji_load.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// ======================> studio2_fujinet_device
//
// Model of the FujiNet RP2354B cartridge for the RCA Studio II. The decision
// for every read is the cartridge's own s2_bus_read (s2_cart.h); the
// protocol (fujimail.c), the codec (fujibus.c), the planner (s2map.c),
// staging (fuji_load.c) and the text engine (s2_text.c) are the firmware's
// sources too. Frames go over a TCP socket to fujinet-pc ($FUJINET_TCP,
// default 127.0.0.1:9995).
//
// The Studio II's slot is a generic ROM socket, so the device is a child of
// the driver, and inert unless the environment asks for it:
//   $FUJINET=1             baked CONFIG at power-on, mailbox live
//   $FUJINET_BOOT=x.st2    that claimed client instead of CONFIG
//   $FUJINET_IMAGE=x       that image alone, no mailbox, no socket (DIRECT)
//   $FUJINET_VIEWDUMP=x    the 64K view at each swap, unclaimed bytes $FF
// Once active it takes every read in the address space -- as on the edge,
// where every read crosses the cartridge -- and gives back the BIOS, the
// console RAM mirror or open bus for the pages it does not claim (the
// cart's CART CS line). Writes are left alone: the edge has no write strobe.
//
// Throttled, transactions run on a worker thread, as on the cart's core0;
// under -nothrottle they run inside the read. $FUJINET_ASYNC=0/1 overrides.
// Lua reads state through save items, never through the address space
// (a Lua read has side effects): see emu/s2text.lua.

class studio2_fujinet_device : public device_t
{
public:
	studio2_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~studio2_fujinet_device();

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override;
	virtual void device_stop() override ATTR_COLD;

private:
	uint8_t read(offs_t offset);
	uint8_t unclaimed(uint16_t a) const;
	uint32_t now_us() const;
	void hotspot(uint16_t a);
	void service(uint16_t a);
	void dispatch(uint16_t a);
	void swapped();
	void worker_main();
	void worker_stop();
	void drain_pokes();
	void publish_staged();
	void dump_view();
	static void load_file(std::vector<uint8_t> &out, const char *path);

	std::vector<uint8_t> m_buf[2];
	uint8_t m_arena[FN_ARENA_SIZE];
	s2_text_t m_text;
	fuji_load_t m_load{};
	s2_view_t *m_view = nullptr;
	s2_bus_t m_bus;

	std::vector<uint8_t> m_boot;            // what power-on serves with the mailbox
	std::vector<uint8_t> m_direct;          // an image served alone, no mailbox
	uint8_t *m_push = nullptr;
	uint32_t m_push_len = 0;
	uint8_t m_own_sel = 0xFF;

	const uint8_t *m_bios = nullptr;        // the console's $0000-$03FF
	uint8_t *m_ram = nullptr;               // the console's 512 bytes
	cpu_device *m_cpu = nullptr;
	output_finder<> m_swaps_out;            // fujinet_swaps, for the harnesses
	output_finder<> m_short_out;            // fujinet_shortlines: DMA bursts < 8
	uint32_t m_short_lines = 0;
	uint16_t m_rline = 0, m_rcol = 0;       // the last raster read's position

	bool m_active = false;                  // the environment asked for us
	bool m_mailbox_mode = false;            // a socket and fujimail, or DIRECT
	bool m_debug = false;
	const char *m_viewdump = nullptr;
	unsigned m_dumps = 0;

	// The worker owns fujimail, the socket, the text engine and staging; the
	// CPU thread owns m_arena and m_view. Pokes reach m_arena through m_pokes.
	bool m_async = false;
	std::thread m_worker;
	std::mutex m_qlock;                     // m_events, m_stop
	std::condition_variable m_qcv;
	std::deque<uint16_t> m_events;
	bool m_stop = false;
	std::mutex m_pklock;                    // m_pokes
	std::vector<std::pair<uint16_t, uint8_t>> m_pokes;
	std::atomic<bool> m_poked{false};
	std::mutex m_loadlock;                  // m_load: staging against the swap
};

// device type definition
DECLARE_DEVICE_TYPE(STUDIO2_FUJINET, studio2_fujinet_device)

#endif // MAME_RCA_STUDIO2_FUJINET_H
