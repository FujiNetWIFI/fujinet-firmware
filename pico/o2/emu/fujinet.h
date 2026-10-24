// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Magnavox Odyssey 2 / Philips Videopac FujiNet cartridge emulation

 The RP2040 cartridge of pico/o2 in fujinet-firmware, for MAME. It runs the
 cartridge's own mailbox service (fujimail.c), image mapper (o2map.c) and
 protocol 2 decode (o2app.h), copied in by pico/o2/emu/apply-mame.sh, and
 reaches a fujinet-pc over TCP ($FUJINET_TCP, default 127.0.0.1:9995).

 Why MAME as well as o2em: MAME's i8244 is the stricter model of what a client
 that rewrites the VDC mid-frame can get away with -- it refuses object
 register writes while the foreground is on, draws per scanline, and runs
 22.75 CPU cycles to the line -- so a client's display engine is timed here.

   mame odyssey2 -cartslot fujinet -cart image.bin

 ***********************************************************************************************************/

#ifndef MAME_BUS_ODYSSEY2_FUJINET_H
#define MAME_BUS_ODYSSEY2_FUJINET_H

#pragma once

#include "slot.h"
#include "o2app.h"
#include "o2map.h"

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
	// The image is loaded after device_start, so the cart comes up here --
	// once: the console's RESET does not reach the cartridge connector, and
	// the cart keeps its state across it (fuji_mailbox.h's ACKSEQ rule).
	virtual void device_reset() override;

private:
	u8 fetch(unsigned addr);
	void take_staged();

	// A legacy image as the bus loop serves it, mailbox page published into
	// every bank; and the next one, until the console fetches $400.
	u8 m_banks[O2MAP_BANKS][O2MAP_BANK_BYTES];
	u8 m_staged[O2MAP_BANKS][O2MAP_BANK_BYTES];

	// Protocol 2: the APPBANK image served through o2app.h, and the next one.
	std::vector<u8> m_app;
	std::vector<u8> m_app_next;
	unsigned m_app_next_pages = 0;
	o2app_t m_appst{};

	u8 m_p1 = 0x13;             // P10, P11, P14 as the driver passes them
	bool m_init_done = false;
	bool m_app_live = false;
	bool m_next_is_app = false;
	bool m_boot_armed = false;
	bool m_publish_staged = false;
	bool m_mailbox = true;
	bool m_debug = false;
};

DECLARE_DEVICE_TYPE(O2_ROM_FUJINET, o2_fujinet_device)

#endif // MAME_BUS_ODYSSEY2_FUJINET_H
