// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_ARCADIA_FUJINET_H
#define MAME_BUS_ARCADIA_FUJINET_H

#pragma once

#include "slot.h"

#include "arcapp.h"

#include <vector>

// ======================> arcadia_rom_fujinet_device
//
// Model of the FujiNet RP2040 Arcadia 2001 cartridge, built from the firmware's own protocol,
// mapper and APPBANK sources; frames go over TCP to fujinet-pc ($FUJINET_TCP, default
// 127.0.0.1:9995). $3000/$5000/$7000 read 0xff; $4000/$6000 alias the blocks, hotspots
// included. An image without the "FUJI" claim runs with the mailbox dead.

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
	// Builds the window once: the image loads after device_start, and the real cart never
	// sees the console's reset line.
	virtual void device_reset() override;

private:
	uint8_t serve(unsigned a14);
	void do_swap();

	// Two slots, as on the cartridge: the push lands in the one not served.
	std::vector<uint8_t> m_slot[2];
	uint8_t m_npages[2] = { 0, 0 };
	unsigned m_live = 0;
	arcapp_t m_app;
	uint8_t m_ff[FN_APP_PAGE];
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
