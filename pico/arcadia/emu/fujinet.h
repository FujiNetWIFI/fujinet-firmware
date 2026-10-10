// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_ARCADIA_FUJINET_H
#define MAME_BUS_ARCADIA_FUJINET_H

#pragma once

#include "slot.h"

#include <vector>

// ======================> arcadia_rom_fujinet_device
//
// Model of the FujiNet RP2040 cartridge for the Emerson Arcadia 2001: an 8K
// image (two 4K blocks) served from RAM, with the read-hotspot mailbox of
// pico/arcadia/firmware/include/fuji_mailbox.h decoded on image offsets
// 0x1D00-0x1FFF (console $2D00-$2FFF) and replies repainted into the
// window. The protocol itself (fujimail.c), the wire codec (fujibus.c) and
// the image mapper (arcmap.c) are the cartridge firmware's own sources,
// compiled in verbatim; this device is only the port: bytes go into the
// served window, frames go over a TCP socket to fujinet-pc ($FUJINET_TCP,
// default 127.0.0.1:9995).
//
// The decode is hardware-faithful via arcmap_decode(): the connector
// carries A0-A13 with A12 as the chip select, so console $3000/$5000/$7000
// read as open bus (0xff, matching the stock STD mapper) while $4000 and
// $6000 alias the two blocks -- $6DFE fires the swap hotspot exactly like
// $2DFE, because on real hardware the cart cannot tell them apart.
//
// An image that does not carry the "FUJI" claim signature runs with the
// mailbox dead -- a plain game ROM -- which is also how a network-booted
// one behaves after the swap.

class arcadia_rom_fujinet_device : public device_t,
							public device_arcadia_cart_interface
{
public:
	arcadia_rom_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~arcadia_rom_fujinet_device();

	virtual uint8_t read_rom(offs_t offset) override;
	virtual uint8_t extra_rom(offs_t offset) override;

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	// The image is loaded after device_start, so the window is built here --
	// once: a machine reset must not reset the "cart", which on hardware
	// never sees the console's reset line at all.
	virtual void device_reset() override;

private:
	uint8_t serve(unsigned a14);
	void do_swap();

	uint8_t m_window[0x2000];
	uint8_t m_staged[0x2000];
	std::vector<uint8_t> m_rx[2];           // per-stream push buffers

	bool m_init_done = false;
	bool m_mailbox_live = false;
	bool m_have_staged = false;
	bool m_staged_claims = false;
	bool m_swap_armed = false;
	bool m_debug = false;
	const char *m_bootdump = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(ARCADIA_ROM_FUJINET, arcadia_rom_fujinet_device)

#endif // MAME_BUS_ARCADIA_FUJINET_H
