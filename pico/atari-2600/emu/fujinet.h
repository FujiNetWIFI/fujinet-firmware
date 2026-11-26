// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_VCS_FUJINET_H
#define MAME_BUS_VCS_FUJINET_H

#pragma once

#include "vcs_slot.h"
#include "vcs_cart.h"
#include "fuji_cfg.h"

#include <vector>

// ======================> a26_rom_fujinet_device
//
// The FujiNet 2600 cartridge's 4K window. fujimail.c, fujibus.c, vcs_render.c
// and the vcs_cart.h decode are the firmware's own sources, grafted in by
// apply.sh; frames go to fujinet-pc over TCP ($FUJINET_TCP, default
// 127.0.0.1:9995). MAME hands a cart a clean byte on a write, so the firmware's
// write sampling (no R/W line) never runs here: test_busio.c covers it.

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
	// No device_reset(): the edge has no reset line, so a console RESET leaves
	// the cart, and its ACKSEQ, alone.

private:
	void ensure_init();
	void do_swap();
	void serve(const uint8_t *img, uint32_t len);

	vcs_mem_t m_mem{};                 // the served window and the decode's state
	std::vector<uint8_t> m_image;      // what is being served
	std::vector<uint8_t> m_staged;     // what a DBC push is building
	std::vector<uint8_t> m_rx;         // the ROM stream
	fuji_cfg_t m_cfg{};                // its .cfg sibling

	bool m_init_done = false;
	bool m_have_staged = false;
	bool m_debug = false;
	const char *m_bootdump = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(A26_ROM_FUJINET, a26_rom_fujinet_device)

#endif // MAME_BUS_VCS_FUJINET_H
