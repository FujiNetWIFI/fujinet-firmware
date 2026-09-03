// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_ASTROCDE_FUJINET_H
#define MAME_BUS_ASTROCDE_FUJINET_H

#pragma once

#include "slot.h"

// ======================> astrocade_rom_fujinet_device
//
// Model of the FujiNet RP2040 cartridge: an 8K window served from RAM, with
// the read-hotspot mailbox of pico/astrocade/firmware/include/fuji_mailbox.h
// decoded on offsets 0x1D00-0x1FFF and replies repainted into the window.
// The protocol itself (fujimail.c), the wire codec (fujibus.c) and the
// image mapper (astromap.c) are the cartridge firmware's own sources,
// compiled in verbatim; this device is only the port: bytes go into the
// served window, frames go over a TCP socket to fujinet-pc ($FUJINET_TCP,
// default 127.0.0.1:9995).
//
// An image that does not carry the "FUJI" claim signature runs as a plain
// 8K ROM with the mailbox dead -- which is also how a network-booted
// ordinary game behaves after the swap.

class astrocade_rom_fujinet_device : public device_t,
							public device_astrocade_cart_interface
{
public:
	astrocade_rom_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~astrocade_rom_fujinet_device();

	virtual uint8_t read_rom(offs_t offset) override;

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	void stream_end(int stream, const uint8_t *data, unsigned len, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	// The image is loaded after device_start, so the window is built here --
	// once: a machine reset must not reset the "cart", which on hardware
	// never sees the console's reset line at all.
	virtual void device_reset() override;

private:
	void do_swap();

	uint8_t m_window[0x2000];
	uint8_t m_staged[0x2000];
	bool m_init_done = false;
	bool m_mailbox_live = false;
	bool m_have_staged = false;
	bool m_staged_claims = false;
	bool m_swap_armed = false;
	bool m_debug = false;
	const char *m_bootdump = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(ASTROCADE_ROM_FUJINET, astrocade_rom_fujinet_device)

#endif // MAME_BUS_ASTROCDE_FUJINET_H
