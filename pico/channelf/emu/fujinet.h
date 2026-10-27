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
// Model of the FujiNet RP2040 Videocart. The cart firmware's fujimail.c,
// fujibus.c, chfmap.c and the channelf_cart.h decode are compiled in by
// pico/channelf/emu/apply.sh; frames go over TCP to fujinet-pc ($FUJINET_TCP,
// default 127.0.0.1:9995).
//
// MAME hands a cart flat reads and writes, so the ROMC state machine is not
// exercised here; firmware/host_test/test_romc.c covers it.

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
	// The image is loaded after device_start, so the window is built here,
	// once: the real cart has no reset line and keeps its state.
	virtual void device_reset() override;

private:
	void do_swap();
	void set_mailbox(bool live);
	void set_image();

	// Whole images, as the cart's two slots: the fixed 16K, then any APPBANK
	// pages. m_live is served; m_staged takes the next push.
	std::vector<uint8_t> m_live;
	std::vector<uint8_t> m_staged;
	uint8_t m_live_np = 0;
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
