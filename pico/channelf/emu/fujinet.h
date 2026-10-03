// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_CHANF_FUJINET_H
#define MAME_BUS_CHANF_FUJINET_H

#pragma once

#include "slot.h"
#include "channelf_cart.h"
#include "chfmap.h"

#include <vector>

// ======================> channelf_fujinet_cartridge_device
//
// Model of the FujiNet RP2040 Videocart: a 16K ROM window at $0800 and a 32K
// arena at $8000 that is part RAM, part painted mailbox. The protocol itself
// (fujimail.c), the wire codec (fujibus.c) and the image mapper (chfmap.c) are
// the cartridge firmware's own sources, compiled in verbatim by
// pico/channelf/emu/apply.sh; this device is only the port -- bytes go into
// the arena, frames go over a TCP socket to fujinet-pc ($FUJINET_TCP, default
// 127.0.0.1:9995).
//
// The address decode is shared too: read_rom() and write_ram() below go
// through chf_read() and chf_write() in channelf_cart.h, the same static
// inlines core1 compiles and the same ones test_busio.c checks. So this device
// and the cartridge cannot disagree about which store is a register write.
//
// What this device does NOT model is the F8 bus. MAME's F8 core keeps one
// internal set of PC0/PC1/DC0/DC1 and calls ROMC_xx() directly, handing a cart
// flat reads and writes -- so every milestone that runs here could pass with a
// completely broken ROMC state machine. That layer is covered separately, by
// replaying golden traces from MAME's own core through chf_bus_cycle(); see
// firmware/host_test/test_romc.c.
//
// One pleasant consequence of the console having a real write cycle: reads are
// inert. There is no read that mutates mailbox state, so nothing here needs a
// side_effects_disabled() guard on the read path, and the MAME debugger can
// browse the whole cartridge window without disturbing a transaction.

class channelf_fujinet_cartridge_device : public device_t,
						public device_channelf_cart_interface
{
public:
	channelf_fujinet_cartridge_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~channelf_fujinet_cartridge_device();

	virtual uint8_t read_rom(offs_t offset) override;
	virtual void write_ram(offs_t offset, uint8_t data) override;

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	// The image is loaded after device_start, so the window is built here --
	// once: a machine reset must not reset the "cart", which on hardware never
	// sees a reset line at all (there is no RESET pin on the connector; the
	// cart infers one from ROMC 08 and deliberately keeps its state).
	virtual void device_reset() override;

private:
	void do_swap();
	void set_mailbox(bool live);

	uint8_t m_window[CHFMAP_WINDOW];   // the 16K ROM window at $0800
	uint8_t m_staged[CHFMAP_WINDOW];
	uint8_t m_arena[FN_ARENA_SIZE];    // 32K at $8000: RAM + painted mailbox
	std::vector<uint8_t> m_rx[2];      // per-stream push buffers

	// The shared decode's view of the above.
	chf_mem_t m_mem{};

	chfmap_plan_t m_staged_plan{};

	bool m_init_done = false;
	bool m_mailbox_live = false;
	bool m_have_staged = false;
	bool m_staged_claims = false;
	bool m_swap_armed = false;
	bool m_debug = false;
	const char *m_bootdump = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(CHANF_ROM_FUJINET, channelf_fujinet_cartridge_device)

#endif // MAME_BUS_CHANF_FUJINET_H
