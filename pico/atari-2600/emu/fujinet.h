// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_VCS_FUJINET_H
#define MAME_BUS_VCS_FUJINET_H

#pragma once

#include "vcs_slot.h"
#include "vcs_cart.h"

#include <vector>

// ======================> a26_rom_fujinet_device
//
// Model of the FujiNet RP2040 cartridge for the Atari 2600: a 4K window at
// $1000-$1FFF that is part banked client image, part painted mailbox, part
// cart-composed text planes. The protocol (fujimail.c), the wire codec
// (fujibus.c) and the glyph compositor (vcs_render.c) are the cartridge
// firmware's own sources, compiled in verbatim by pico/atari-2600/emu/apply.sh;
// this device is only the port -- bytes go into the window, frames go over a
// TCP socket to fujinet-pc ($FUJINET_TCP, default 127.0.0.1:9995).
//
// The address decode is shared: read() and write() below go through
// vcs_read() and vcs_write() in vcs_cart.h, the same static inlines core1
// compiles and the same ones test_busio.c checks. So this device and the
// cartridge cannot disagree about which store is a register write.
//
// WHAT THIS DEVICE DOES NOT MODEL, and it is the important one. MAME hands a
// cart device a clean data byte on a write. Real hardware has no R/W line: the
// cartridge parks on the stable address and recovers the byte from the
// second-to-last data sample. That sampling -- the single riskiest thing in
// the port -- therefore never runs here. It is covered instead by
// firmware/host_test/test_busio.c, and backstopped by the fact that the exact
// idiom ships in every PlusROM game and every Superchip cartridge.
//
// One pleasant consequence of the console having a real store cycle: no read
// in the mailbox window has a side effect. Nothing here needs a
// side_effects_disabled() guard on the read path, and the MAME debugger can
// browse the whole cartridge window without disturbing a transaction. (A
// BOOTED GAME is a different matter -- every classic 2600 mapper switches
// banks on reads -- and that guard arrives with vcsmap at M2.)

class a26_rom_fujinet_device : public device_t, public device_vcs_cart_interface
{
public:
	a26_rom_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~a26_rom_fujinet_device();

	virtual void install_memory_handlers(address_space *space) override;

	uint8_t read(offs_t offset);
	void write(offs_t offset, uint8_t data);

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	// Deliberately no device_reset(): there is no reset line on the cartridge
	// connector, and the 2600's RESET switch is a bit in a RIOT register the
	// cart never sees. A console reset must NOT reset the cart -- keeping
	// ACKSEQ across it is exactly what the client's "next sequence is the
	// cart's persisted ACKSEQ + 1" rule tests.

private:
	void ensure_init();
	void do_swap();
	void serve(const uint8_t *img, uint32_t len);

	vcs_mem_t m_mem{};                 // the served window and the decode's state
	std::vector<uint8_t> m_image;      // what is being served
	std::vector<uint8_t> m_staged;     // what a DBC push is building
	std::vector<uint8_t> m_rx[2];      // per-stream push buffers

	bool m_init_done = false;
	bool m_have_staged = false;
	bool m_debug = false;
	const char *m_bootdump = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(A26_ROM_FUJINET, a26_rom_fujinet_device)

#endif // MAME_BUS_VCS_FUJINET_H
