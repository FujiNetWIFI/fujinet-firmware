// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_A800_A5200_FUJINET_H
#define MAME_BUS_A800_A5200_FUJINET_H

#pragma once

#include "a800_slot.h"

#include "a52map.h"
#include "fuji_load.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// ======================> a5200_fujinet_device
//
// Model of the FujiNet RP2354B cartridge: the 32K window served from one of
// two view buffers, with the read-hotspot mailbox of
// pico/atari-5200/firmware/include/fuji_mailbox.h at $B000-$B7FF. The
// protocol (fujimail.c), the wire codec (fujibus.c), the mappers (a52map.c)
// and staging (fuji_load.c) are the cartridge firmware's own sources; this
// device is only the port: frames go over a TCP socket to fujinet-pc
// ($FUJINET_TCP, default 127.0.0.1:9995).
//
// What it serves at power-on:
//   -cart with the "FUJI" claim  that client, mailbox live (as the baked CONFIG)
//   -cart without it             that image, mailbox off, no socket (DIRECT)
//   $FUJINET_IMAGE               the same, from a path
//   no -cart                     the baked CONFIG
// $FUJINET_MAPPER is the .cfg override; $FUJINET_MERGE=1 merges back-to-back
// cycles at one address, as a 4-port console's clockless cart sees them;
// $FUJINET_VIEWDUMP writes the 32K window at each swap.
//
// Throttled, transactions run on a worker thread, as the cart's core0 runs
// them while core1 serves: the console polls and the window keeps drawing.
// Under -nothrottle (the harnesses) they run inside the read, so emulated
// time cannot outrun a network wait. $FUJINET_ASYNC=0/1 overrides.
//
// The real cart cannot tell a write from a read, so write() has exactly the
// side effects read() does. Every reset is a power cycle: the 5200 has no
// reset line, and MAME's resets stand in for its power switch.

class a5200_fujinet_device : public device_t, public device_a5200_cart_interface
{
public:
	a5200_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~a5200_fujinet_device();

	virtual void cart_map(address_map &map) override ATTR_COLD;

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
	void write(offs_t offset, uint8_t data);
	void access(offs_t offset);
	void hotspot(uint16_t a);
	void service(uint16_t a);
	void mailbox_read(uint16_t a);
	void swapped();
	void worker_main();
	void worker_stop();
	void drain_pokes();
	void publish_staged();
	void dump_view();
	static void load_file(std::vector<uint8_t> &out, const char *path);

	std::vector<uint8_t> m_buf[2];
	uint8_t m_arena[FN_ARENA_SIZE];
	fuji_load_t m_load{};
	a52_view_t *m_view = nullptr;

	std::vector<uint8_t> m_boot;            // what power-on serves with the mailbox
	std::vector<uint8_t> m_direct;          // an image served alone, no mailbox
	std::vector<uint8_t> m_cfg;             // the .cfg sibling
	uint8_t *m_push = nullptr;
	uint32_t m_push_len = 0;
	std::string m_cfg_mapper;
	uint8_t m_own_sel = 0xFF;

	output_finder<> m_swaps_out;            // fujinet_swaps, for the harnesses
	cpu_device *m_cpu = nullptr;
	bool m_merge = false;
	offs_t m_last_off = ~offs_t(0);
	uint64_t m_last_cycle = 0;

	bool m_mailbox_mode = false;            // a socket and fujimail, or DIRECT
	bool m_debug = false;
	const char *m_viewdump = nullptr;
	unsigned m_dumps = 0;

	// The worker owns fujimail, the socket and staging; the CPU thread owns
	// m_arena and m_view. Pokes reach m_arena through m_pokes.
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
DECLARE_DEVICE_TYPE(A5200_FUJINET, a5200_fujinet_device)

#endif // MAME_BUS_A800_A5200_FUJINET_H
